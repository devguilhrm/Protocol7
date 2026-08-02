   #include <stdio.h>
   #include <string.h>
   #include "config.h"
   #include "server.h"
   #include "logger.h"

   int main(int argc, char* argv[]) {
       if (argc != 2 || strncmp(argv[1], "--config=", 9) != 0) {
           fprintf(stderr, "Usage: %s --config=<file.toml>\n", argv[0]);
           return 1;
       }

       const char* config_file = argv[1] + 9;
       Config config;
       if (parse_config(config_file, &config) != 0) return 1;

       run_server(&config);
       return 0;
   }