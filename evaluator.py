"""Bounded HTTP/1.x transport helpers for the public evaluator."""

from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
from enum import Enum
import http.client
import math
import os
import socket
import sys
import threading
import time
import traceback
from collections.abc import Callable, Sequence
from typing import TextIO


LOOPBACK = "127.0.0.1"
MAX_HEADER_BYTES = 65_536
MAX_BODY_BYTES = 8_388_608
SYNC_TIMEOUT_SECONDS = 2.0
_DEBUG = False

ALPHA_INDEX = b"<h1>alpha</h1>\n"
ALPHA_EMPTY_FILE = b""
ALPHA_BINARY_FILE = bytes(range(256))
ALPHA_NESTED_TEXT = b"hello from alpha\n"
BETA_INDEX = b"<h1>beta</h1>\n"
BETA_SHARED_TEXT = b"shared from beta\n"
OUTSIDE_SENTINEL = b"traversal sentinel: 7c0df6d7\n"
_FUNDAMENTAL_CHECK_NAMES = frozenset({"alpha index", "beta index"})


@dataclass(frozen=True, slots=True)
class HttpResponse:
    version: str
    status: int
    reason: str
    headers: dict[str, tuple[str, ...]]
    body: bytes


class ProtocolError(RuntimeError):
    pass


class ResultState(str, Enum):
    PASS = "PASS"
    FAIL = "FAIL"
    SKIP = "SKIP"


@dataclass(frozen=True, slots=True)
class Check:
    name: str
    function: Callable[[], str]
    required: bool = True


@dataclass(frozen=True, slots=True)
class CheckResult:
    name: str
    state: ResultState
    detail: str
    required: bool


@dataclass(frozen=True, slots=True)
class BenchmarkResult:
    name: str
    requests: int
    successes: int
    incorrect: int
    errors: int
    duration_seconds: float
    requests_per_second: float
    p50_ms: float | None
    p95_ms: float | None
    p99_ms: float | None


def run_checks(checks: tuple[Check, ...]) -> list[CheckResult]:
    """Run each conformance check without allowing one failure to stop the rest."""
    results: list[CheckResult] = []
    expected_errors = (
        AssertionError,
        ProtocolError,
        OSError,
        TimeoutError,
        http.client.HTTPException,
    )
    for check in checks:
        try:
            detail = check.function()
        except expected_errors as error:
            state = ResultState.FAIL if check.required else ResultState.SKIP
            results.append(CheckResult(check.name, state, str(error), check.required))
        except Exception as error:
            detail = f"{type(error).__name__}: {error}"
            if _DEBUG:
                detail = traceback.format_exc()
            state = ResultState.FAIL if check.required else ResultState.SKIP
            results.append(CheckResult(check.name, state, detail, check.required))
        else:
            results.append(
                CheckResult(check.name, ResultState.PASS, detail, check.required)
            )
    return results


class Reporter:
    _COLORS = {
        ResultState.PASS: "\033[32m",
        ResultState.FAIL: "\033[31m",
        ResultState.SKIP: "\033[33m",
    }
    _RESET = "\033[0m"

    def __init__(self, stream: TextIO, use_color: bool) -> None:
        self._stream = stream
        self._use_color = use_color and stream.isatty() and "NO_COLOR" not in os.environ

    def check(self, result: CheckResult) -> None:
        self._stream.write(
            f"{self._status(result.state)} {self._one_line(result.name)}: "
            f"{self._one_line(result.detail)}\n"
        )

    def benchmark(self, name: str, detail: str) -> None:
        self._stream.write(f"BENCHMARK {name}: {detail}\n")

    def summary(self, results: list[CheckResult]) -> None:
        counts = {state: 0 for state in ResultState}
        for result in results:
            counts[result.state] += 1
        self._stream.write(
            "Summary: "
            f"{counts[ResultState.PASS]} passed, "
            f"{counts[ResultState.FAIL]} failed, "
            f"{counts[ResultState.SKIP]} skipped\n"
        )

    def _status(self, state: ResultState) -> str:
        if not self._use_color:
            return state.value
        return f"{self._COLORS[state]}{state.value}{self._RESET}"

    @staticmethod
    def _one_line(value: str) -> str:
        return value.replace("\r", "\\r").replace("\n", "\\n")


class _HeaderBoundedReader:
    """Count raw HTTP response header bytes before the stdlib normalizes them."""

    def __init__(self, reader: object) -> None:
        self._reader = reader
        self._header_bytes = 0

    def readline(self, limit: int = -1) -> bytes:
        line = self._reader.readline(limit)
        self._header_bytes += len(line)
        if self._header_bytes > MAX_HEADER_BYTES:
            raise ProtocolError("headers too large")
        return line

    def __getattr__(self, name: str) -> object:
        return getattr(self._reader, name)


class _BoundedHTTPResponse(http.client.HTTPResponse):
    """Keep http.client parsing while bounding raw status and header bytes."""

    def begin(self) -> None:
        self.fp = _HeaderBoundedReader(self.fp)
        super().begin()


def nearest_rank(values: list[float], percentile: int) -> float:
    if not values:
        raise ValueError("cannot calculate a percentile without values")
    ordered = sorted(values)
    rank = max(1, math.ceil(percentile / 100 * len(ordered)))
    return ordered[rank - 1]


def _recv_until(
    connection: socket.socket, delimiter: bytes, limit: int, timeout: float
) -> tuple[bytes, bytes]:
    """Read through *delimiter* without allowing the buffered value past *limit*."""
    buffered = bytearray()
    connection.settimeout(timeout)
    while True:
        delimiter_index = buffered.find(delimiter)
        if delimiter_index != -1:
            end = delimiter_index + len(delimiter)
            if end > limit:
                raise ProtocolError("headers too large")
            return bytes(buffered[:end]), bytes(buffered[end:])
        if len(buffered) >= limit:
            raise ProtocolError("headers too large")
        try:
            chunk = connection.recv(min(4_096, limit - len(buffered) + len(delimiter)))
        except TimeoutError as error:
            raise ProtocolError("timed out while reading headers") from error
        if not chunk:
            raise ProtocolError("truncated headers")
        buffered.extend(chunk)


def _recv_buffer_until(
    connection: socket.socket,
    buffered: bytearray,
    delimiter: bytes,
    limit: int,
    timeout: float,
    description: str,
) -> bytes:
    """Consume through a delimiter while bounding only the delimited prefix."""
    connection.settimeout(timeout)
    while True:
        delimiter_index = buffered.find(delimiter)
        if delimiter_index != -1:
            end = delimiter_index + len(delimiter)
            if end > limit:
                raise ProtocolError(f"{description} too large")
            value = bytes(buffered[:end])
            del buffered[:end]
            return value
        if len(buffered) >= limit:
            raise ProtocolError(f"{description} too large")
        try:
            chunk = connection.recv(
                min(4_096, limit - len(buffered) + len(delimiter))
            )
        except TimeoutError as error:
            raise ProtocolError("timed out while reading body") from error
        if not chunk:
            raise ProtocolError("truncated body")
        buffered.extend(chunk)


def _recv_buffer_exact(
    connection: socket.socket,
    buffered: bytearray,
    length: int,
    timeout: float,
) -> bytes:
    """Consume exactly *length* bytes while preserving coalesced bytes."""
    connection.settimeout(timeout)
    while len(buffered) < length:
        try:
            chunk = connection.recv(length - len(buffered))
        except TimeoutError as error:
            raise ProtocolError("timed out while reading body") from error
        if not chunk:
            raise ProtocolError("truncated body")
        buffered.extend(chunk)
    value = bytes(buffered[:length])
    del buffered[:length]
    return value


def _recv_exact(
    connection: socket.socket, length: int, initial: bytes, timeout: float
) -> bytes:
    """Return exactly *length* bytes or report a truncated body."""
    buffered = bytearray(initial[:length])
    connection.settimeout(timeout)
    while len(buffered) < length:
        try:
            chunk = connection.recv(length - len(buffered))
        except TimeoutError as error:
            raise ProtocolError("timed out while reading body") from error
        if not chunk:
            raise ProtocolError("truncated body")
        buffered.extend(chunk)
    return bytes(buffered)


def _recv_to_eof(connection: socket.socket, initial: bytes, timeout: float) -> bytes:
    """Read a close-delimited body while enforcing the body size limit."""
    buffered = bytearray(initial)
    if len(buffered) > MAX_BODY_BYTES:
        raise ProtocolError("body too large")
    connection.settimeout(timeout)
    while True:
        try:
            chunk = connection.recv(min(4_096, MAX_BODY_BYTES - len(buffered) + 1))
        except TimeoutError as error:
            raise ProtocolError("timed out while reading body") from error
        if not chunk:
            return bytes(buffered)
        buffered.extend(chunk)
        if len(buffered) > MAX_BODY_BYTES:
            raise ProtocolError("body too large")


def _parse_status_line(line: bytes) -> tuple[str, int, str]:
    try:
        version, status_text, reason = line.decode("iso-8859-1").split(" ", 2)
    except ValueError as error:
        raise ProtocolError("invalid status line") from error
    if version not in {"HTTP/1.0", "HTTP/1.1"}:
        raise ProtocolError("invalid status line")
    if len(status_text) != 3 or not status_text.isascii() or not status_text.isdecimal():
        raise ProtocolError("invalid status line")
    return version, int(status_text), reason


def _parse_headers(lines: list[bytes]) -> dict[str, tuple[str, ...]]:
    values: dict[str, list[str]] = {}
    for line in lines:
        if b":" not in line:
            raise ProtocolError("invalid header")
        name_bytes, value_bytes = line.split(b":", 1)
        if not name_bytes:
            raise ProtocolError("invalid header")
        try:
            name = name_bytes.decode("ascii").lower()
            value = value_bytes.decode("iso-8859-1").strip(" \t")
        except UnicodeDecodeError as error:
            raise ProtocolError("invalid header") from error
        values.setdefault(name, []).append(value)
    return {name: tuple(field_values) for name, field_values in values.items()}


def _content_length(headers: dict[str, tuple[str, ...]]) -> int | None:
    values = headers.get("content-length")
    if values is None:
        return None
    if len(values) != 1:
        raise ProtocolError("invalid Content-Length")
    value = values[0]
    if not value or not value.isascii() or not value.isdecimal():
        raise ProtocolError("invalid Content-Length")
    length = int(value)
    if length > MAX_BODY_BYTES:
        raise ProtocolError("body too large")
    return length


def _uses_chunked_encoding(
    version: str, headers: dict[str, tuple[str, ...]]
) -> bool:
    values = headers.get("transfer-encoding")
    if values is None:
        return False
    if version != "HTTP/1.1":
        raise ProtocolError("Transfer-Encoding requires HTTP/1.1")
    codings = [coding.strip().lower() for value in values for coding in value.split(",")]
    if codings != ["chunked"]:
        raise ProtocolError("unsupported Transfer-Encoding")
    return True


def _recv_chunked(
    connection: socket.socket, initial: bytes, timeout: float
) -> bytes:
    """Decode one bounded HTTP/1.1 chunked body, including bounded trailers."""
    buffered = bytearray(initial)
    body = bytearray()
    while True:
        size_line = _recv_buffer_until(
            connection,
            buffered,
            b"\r\n",
            MAX_HEADER_BYTES,
            timeout,
            "chunk header",
        )[:-2]
        size_text = size_line.split(b";", 1)[0]
        if not size_text or any(byte not in b"0123456789abcdefABCDEF" for byte in size_text):
            raise ProtocolError("invalid chunk size")
        size = int(size_text, 16)
        if size > MAX_BODY_BYTES - len(body):
            raise ProtocolError("body too large")
        if size == 0:
            trailer_bytes = 0
            while True:
                trailer_line = _recv_buffer_until(
                    connection,
                    buffered,
                    b"\r\n",
                    MAX_HEADER_BYTES - trailer_bytes,
                    timeout,
                    "trailers",
                )
                trailer_bytes += len(trailer_line)
                if trailer_line == b"\r\n":
                    return bytes(body)
                _parse_headers([trailer_line[:-2]])
        chunk = _recv_buffer_exact(connection, buffered, size + 2, timeout)
        if not chunk.endswith(b"\r\n"):
            raise ProtocolError("invalid chunk framing")
        body.extend(chunk[:-2])


def _read_response(connection: socket.socket, timeout: float) -> HttpResponse:
    header_bytes, initial_body = _recv_until(
        connection, b"\r\n\r\n", MAX_HEADER_BYTES, timeout
    )
    lines = header_bytes[:-4].split(b"\r\n")
    version, status, reason = _parse_status_line(lines[0])
    headers = _parse_headers(lines[1:])
    chunked = _uses_chunked_encoding(version, headers)
    content_length = _content_length(headers)
    if chunked and content_length is not None:
        raise ProtocolError("ambiguous response framing")
    if chunked:
        body = _recv_chunked(connection, initial_body, timeout)
    elif content_length is None:
        body = _recv_to_eof(connection, initial_body, timeout)
    else:
        body = _recv_exact(connection, content_length, initial_body, timeout)
    return HttpResponse(version, status, reason, headers, body)


def _normalize_http_response(response: http.client.HTTPResponse) -> HttpResponse:
    headers: dict[str, list[str]] = {}
    header_items = response.getheaders()
    version = {10: "HTTP/1.0", 11: "HTTP/1.1"}.get(response.version)
    if version is None:
        raise ProtocolError("invalid status line")
    for name, value in header_items:
        headers.setdefault(name.lower(), []).append(value)
    normalized_headers = {name: tuple(values) for name, values in headers.items()}
    chunked = _uses_chunked_encoding(version, normalized_headers)
    content_length = _content_length(normalized_headers)
    if chunked and content_length is not None:
        raise ProtocolError("ambiguous response framing")
    body = response.read(MAX_BODY_BYTES + 1)
    if len(body) > MAX_BODY_BYTES:
        raise ProtocolError("body too large")
    if content_length is not None and len(body) != content_length:
        raise ProtocolError("truncated body")
    return HttpResponse(
        version=version,
        status=response.status,
        reason=response.reason,
        headers=normalized_headers,
        body=body,
    )


def request_http(
    port: int, host: str, target: str, timeout: float = 2.0
) -> HttpResponse:
    """Send a bounded HTTP GET request and normalize its HTTP/1.x response."""
    connection = http.client.HTTPConnection(LOOPBACK, port, timeout=timeout)
    try:
        connection.request("GET", target, headers={"Host": host})
        connection.response_class = _BoundedHTTPResponse
        return _normalize_http_response(connection.getresponse())
    finally:
        connection.close()


def _measure_one(port: int) -> tuple[bool, bool, float | None]:
    """Measure one alpha fixture request and classify its observable outcome."""
    start_ns = time.perf_counter_ns()
    try:
        response = request_http(port, "alpha.com", "/index.html")
    except (OSError, TimeoutError, ProtocolError, http.client.HTTPException):
        time.perf_counter_ns()
        return False, False, None
    elapsed_ms = (time.perf_counter_ns() - start_ns) / 1_000_000
    if response.status == 200 and response.body == ALPHA_INDEX:
        return True, False, elapsed_ms
    return False, True, None


def _benchmark_result(
    name: str,
    requests: int,
    measurements: list[tuple[bool, bool, float | None]],
    start_ns: int,
    end_ns: int,
) -> BenchmarkResult:
    successful_latencies = [
        latency for success, _, latency in measurements if success and latency is not None
    ]
    successes = len(successful_latencies)
    incorrect = sum(incorrect for _, incorrect, _ in measurements)
    errors = requests - successes - incorrect
    duration_seconds = (end_ns - start_ns) / 1_000_000_000
    requests_per_second = (
        successes / duration_seconds if duration_seconds > 0 else 0.0
    )
    if not successful_latencies:
        p50_ms = p95_ms = p99_ms = None
    else:
        p50_ms = nearest_rank(successful_latencies, 50)
        p95_ms = nearest_rank(successful_latencies, 95)
        p99_ms = nearest_rank(successful_latencies, 99)
    return BenchmarkResult(
        name,
        requests,
        successes,
        incorrect,
        errors,
        duration_seconds,
        requests_per_second,
        p50_ms,
        p95_ms,
        p99_ms,
    )


def run_benchmarks(
    port: int,
    warmup: int = 20,
    sequential_requests: int = 500,
    virtual_users: int = 32,
    requests_per_vu: int = 50,
) -> tuple[BenchmarkResult, BenchmarkResult]:
    """Measure sequential and bounded-concurrent alpha fixture requests."""
    for _ in range(warmup):
        try:
            request_http(port, "alpha.com", "/index.html")
        except (OSError, TimeoutError, ProtocolError, http.client.HTTPException):
            pass

    sequential_start_ns = time.perf_counter_ns()
    sequential_measurements = [_measure_one(port) for _ in range(sequential_requests)]
    sequential_end_ns = time.perf_counter_ns()
    sequential = _benchmark_result(
        "single consumer",
        sequential_requests,
        sequential_measurements,
        sequential_start_ns,
        sequential_end_ns,
    )

    workers_ready = threading.Barrier(virtual_users + 1)
    start_gate = threading.Event()

    def run_virtual_user(_: int) -> list[tuple[bool, bool, float | None]]:
        workers_ready.wait(timeout=SYNC_TIMEOUT_SECONDS)
        if not start_gate.wait(timeout=SYNC_TIMEOUT_SECONDS):
            raise TimeoutError("benchmark start synchronization timed out")
        return [_measure_one(port) for _ in range(requests_per_vu)]

    with ThreadPoolExecutor(max_workers=virtual_users) as executor:
        try:
            futures = [
                executor.submit(run_virtual_user, index)
                for index in range(virtual_users)
            ]
            workers_ready.wait(timeout=SYNC_TIMEOUT_SECONDS)
            concurrent_start_ns = time.perf_counter_ns()
            start_gate.set()
            concurrent_measurements = [
                measurement
                for future in futures
                for measurement in future.result()
            ]
            concurrent_end_ns = time.perf_counter_ns()
        finally:
            workers_ready.abort()
            start_gate.set()
    concurrent = _benchmark_result(
        "32 VUs",
        virtual_users * requests_per_vu,
        concurrent_measurements,
        concurrent_start_ns,
        concurrent_end_ns,
    )
    return sequential, concurrent


def request_raw(
    port: int,
    chunks: tuple[bytes, ...],
    timeout: float = 2.0,
    pause: float = 0.0,
) -> HttpResponse:
    """Send raw request chunks to loopback, half-close, then read one response."""
    with socket.create_connection((LOOPBACK, port), timeout=timeout) as connection:
        for index, chunk in enumerate(chunks):
            connection.sendall(chunk)
            if pause and index < len(chunks) - 1:
                time.sleep(pause)
        connection.shutdown(socket.SHUT_WR)
        return _read_response(connection, timeout)


def _expect_status(response: HttpResponse, expected: int) -> None:
    if response.status != expected:
        raise AssertionError(f"expected HTTP {expected}, received HTTP {response.status}")


def _expect_safe_rejection(response: HttpResponse) -> None:
    if response.status not in {400, 403, 404}:
        raise AssertionError(
            f"expected HTTP 400, 403, or 404; received HTTP {response.status}"
        )
    if OUTSIDE_SENTINEL in response.body:
        raise AssertionError("response exposed content outside the configured site root")


def _expect_body(response: HttpResponse, expected: bytes) -> None:
    if response.body != expected:
        raise AssertionError("response body did not match expected fixture bytes")


def _expect_no_fixture_content(response: HttpResponse) -> None:
    fixture_bodies = (
        ALPHA_INDEX,
        ALPHA_NESTED_TEXT,
        ALPHA_BINARY_FILE,
        BETA_INDEX,
        BETA_SHARED_TEXT,
        OUTSIDE_SENTINEL,
    )
    if any(body in response.body for body in fixture_bodies):
        raise AssertionError("response exposed fixture content for a missing file")


def build_conformance_checks(port: int) -> tuple[Check, ...]:
    """Build the deterministic public HTTP/1.1 conformance registry."""

    def expect_file(host: str, target: str, expected: bytes) -> Callable[[], str]:
        def check() -> str:
            response = request_http(port, host, target)
            _expect_status(response, 200)
            _expect_body(response, expected)
            return "200 with expected fixture bytes"

        return check

    def check_missing_file() -> str:
        response = request_http(port, "alpha.com", "/missing.txt")
        _expect_status(response, 404)
        _expect_no_fixture_content(response)
        return "404 without fixture content"

    def check_query_separation() -> str:
        response = request_http(port, "alpha.com", "/index.html?ignored=query")
        _expect_status(response, 200)
        _expect_body(response, ALPHA_INDEX)
        return "query did not alter the file target"

    def check_beta_host_routing() -> str:
        response = request_http(port, "beta.com", "/index.html")
        _expect_status(response, 200)
        _expect_body(response, BETA_INDEX)
        unknown = request_http(port, "unknown.invalid", "/index.html")
        _expect_status(unknown, 404)
        _expect_no_fixture_content(unknown)
        return "beta fixture and unknown-host 404 routing"

    def check_fragmented_input() -> str:
        response = request_raw(
            port,
            (
                b"GET /nested/hello",
                b".txt HTTP/1.1\r\nHost: alpha.com\r\n",
                b"\r\n",
            ),
            pause=0.01,
        )
        _expect_status(response, 200)
        _expect_body(response, ALPHA_NESTED_TEXT)
        return "accepted a fragmented request"

    def check_missing_host() -> str:
        response = request_raw(port, (b"GET /index.html HTTP/1.1\r\n\r\n",))
        _expect_status(response, 400)
        return "rejected a request without Host"

    def check_duplicate_host() -> str:
        response = request_raw(
            port,
            (
                b"GET /index.html HTTP/1.1\r\n"
                b"Host: alpha.com\r\n"
                b"Host: beta.com\r\n\r\n",
            ),
        )
        _expect_status(response, 400)
        return "rejected duplicate Host fields"

    def check_malformed_request_line() -> str:
        response = request_raw(
            port, (b"GET /index.html\r\nHost: alpha.com\r\n\r\n",)
        )
        _expect_status(response, 400)
        return "rejected a malformed request line"

    def check_literal_traversal() -> str:
        response = request_http(port, "alpha.com", "/../../outside-secret.txt")
        _expect_safe_rejection(response)
        return "contained literal traversal"

    def check_percent_encoded_traversal() -> str:
        response = request_http(
            port, "alpha.com", "/%2e%2e/%2e%2e/outside-secret.txt"
        )
        _expect_safe_rejection(response)
        return "contained percent-encoded traversal"

    def check_concurrent_sanity() -> str:
        barrier = threading.Barrier(32)

        def send_one(_: int) -> None:
            barrier.wait(timeout=SYNC_TIMEOUT_SECONDS)
            response = request_http(port, "alpha.com", "/index.html")
            _expect_status(response, 200)
            _expect_body(response, ALPHA_INDEX)

        with ThreadPoolExecutor(max_workers=32) as executor:
            try:
                list(executor.map(send_one, range(32)))
            finally:
                barrier.abort()
        return "32 concurrent requests returned the alpha fixture"

    def check_response_framing() -> str:
        response = request_raw(
            port, (b"GET /index.html HTTP/1.1\r\nHost: alpha.com\r\n\r\n",)
        )
        _expect_status(response, 200)
        if response.version not in {"HTTP/1.0", "HTTP/1.1"}:
            raise AssertionError("response did not use a valid HTTP/1.x status line")
        lengths = response.headers.get("content-length")
        if lengths is not None:
            if len(lengths) != 1 or not lengths[0].isdecimal():
                raise AssertionError("response used an invalid Content-Length")
            if int(lengths[0]) != len(response.body):
                raise AssertionError("response body did not match Content-Length")
        return "valid HTTP/1.x response framing"

    return (
        Check("alpha index", expect_file("alpha.com", "/index.html", ALPHA_INDEX)),
        Check("beta index", check_beta_host_routing),
        Check(
            "nested text",
            expect_file("alpha.com", "/nested/hello.txt", ALPHA_NESTED_TEXT),
        ),
        Check("empty file", expect_file("alpha.com", "/empty.txt", ALPHA_EMPTY_FILE)),
        Check("binary file", expect_file("alpha.com", "/binary.bin", ALPHA_BINARY_FILE)),
        Check("missing file", check_missing_file),
        Check("query separation", check_query_separation),
        Check("fragmented input", check_fragmented_input),
        Check("missing Host", check_missing_host),
        Check("duplicate Host", check_duplicate_host),
        Check("malformed request line", check_malformed_request_line),
        Check("literal traversal", check_literal_traversal),
        Check("percent-encoded traversal", check_percent_encoded_traversal),
        Check("32 concurrent sanity requests", check_concurrent_sanity),
        Check("response framing", check_response_framing),
    )


def fundamental_checks_passed(results: list[CheckResult]) -> bool:
    """Return whether both public host-routing checks completed successfully."""
    states = {result.name: result.state for result in results}
    return all(
        states.get(name) is ResultState.PASS for name in _FUNDAMENTAL_CHECK_NAMES
    )


def _port(value: str) -> int:
    try:
        port = int(value)
    except ValueError as error:
        raise argparse.ArgumentTypeError("port must be an integer") from error
    if not 1 <= port <= 65_535:
        raise argparse.ArgumentTypeError("port must be between 1 and 65535")
    return port


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    """Parse the public evaluator command line."""
    parser = argparse.ArgumentParser(
        description="Evaluate a local HTTP hackathon server."
    )
    parser.add_argument(
        "--port",
        required=True,
        type=_port,
        metavar="PORT",
        help="participant server port (1-65535)",
    )
    return parser.parse_args(argv)


def _format_latency(value: float | None) -> str:
    return "n/a" if value is None else f"{value:.3f} ms"


def _format_benchmark(result: BenchmarkResult) -> str:
    return (
        f"requests={result.requests}, successes={result.successes}, "
        f"incorrect={result.incorrect}, errors={result.errors}, "
        f"duration={result.duration_seconds:.3f} s, "
        f"throughput={result.requests_per_second:.2f} requests/s, "
        f"p50={_format_latency(result.p50_ms)}, "
        f"p95={_format_latency(result.p95_ms)}, "
        f"p99={_format_latency(result.p99_ms)}"
    )


def _run(argv: Sequence[str] | None, stream: TextIO) -> int:
    args = parse_args(argv)

    stream.write("== Preflight ==\n")
    try:
        with socket.create_connection((LOOPBACK, args.port), timeout=2.0):
            pass
    except (OSError, TimeoutError) as error:
        stream.write(f"Unable to connect to {LOOPBACK}:{args.port}: {error}\n")
        return 2
    stream.write(f"Connected to {LOOPBACK}:{args.port}\n")

    stream.write("\n== Conformance ==\n")
    results = run_checks(build_conformance_checks(args.port))
    reporter = Reporter(stream, use_color=True)
    for result in results:
        reporter.check(result)
    reporter.summary(results)

    required_passed = all(
        not result.required or result.state is ResultState.PASS for result in results
    )
    if required_passed:
        stream.write("All required checks passed\n")
    else:
        stream.write("Required checks failed\n")

    stream.write("\n== Benchmarks ==\n")
    if fundamental_checks_passed(results):
        try:
            benchmarks = run_benchmarks(args.port)
        except Exception as error:
            detail = Reporter._one_line(f"{type(error).__name__}: {error}")
            stream.write(f"Benchmarks unavailable: {detail}\n")
        else:
            for benchmark in benchmarks:
                reporter.benchmark(benchmark.name, _format_benchmark(benchmark))
    else:
        stream.write("Benchmarks skipped: fundamental host-routing checks failed\n")

    return 0 if required_passed else 1


def main(
    argv: Sequence[str] | None = None,
    stream: TextIO = sys.stdout,
) -> int:
    """Run the public evaluator and return its process exit status."""
    try:
        return _run(argv, stream)
    except KeyboardInterrupt:
        stream.write("\nInterrupted\n")
        return 130


if __name__ == "__main__":
    raise SystemExit(main())
