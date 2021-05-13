#include "madrinha.h"


int main(int argc, const char *argv[]) {

  madrinha::config config;

  auto ret = madrinha::parser_arguments(argc, argv, config);
  if (ret) {
    exit(ret);
  }

  madrinha::madrinha_server server(config);

  server.run();
  
  return 0;
}

