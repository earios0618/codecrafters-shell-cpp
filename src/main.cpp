#include <iostream>
#include <string>
#include <sstream>

#ifdef _WIN32
#include <io.h>
#define access _access
#ifndef X_OK
#define X_OK 0
#endif
#else
#include <unistd.h>
#endif

#include "commands.h"


Command parse_command(std::string commandLine);

int main() {
  // Flush after every std::cout / std:cerr
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;
  while (true) {
    std::cout << "$ ";
    std::string commandLine;
    std::getline(std::cin, commandLine);
    Command command = parse_command(commandLine);
    //special command case
    if (command == CMD_EXIT) {
      break;
    }
    switch (command) {
      case CMD_ECHO:
        std::cout << commandLine.substr(5) << std::endl;
        break;
      case CMD_TYPE: {
        std::string parameter = commandLine.substr(5);
        Command subCommand = parse_command(parameter);
        if (subCommand == CMD_INVALID) {
          std::string path_env = std::getenv("PATH");
          std::stringstream ss_path(path_env);
          std::string path;
          bool found = false;
          while (std::getline(ss_path, path, ':')) {
            std::string full_path = path + '/' + parameter;
            if (access(full_path.c_str(), X_OK) == 0) {
              std::cout << parameter << " is " << full_path << std::endl;
              found = true;
              break;
            }
          }
          if (found) { break; }
          std::cout << parameter + ": not found\n";
        } else {
          std::cout << parameter + " is a shell builtin\n";
        }
        break;
      }
      default:
        std::cout << commandLine + ": command not found\n";
        break;
    }
  }
}

Command parse_command(std::string commandLine) {
  if (commandLine == "exit") {
    return CMD_EXIT;
  } else if (commandLine.substr(0, 4) == "type"){
    return CMD_TYPE;
  } else if (commandLine.substr(0, 4) == "echo") {
    return CMD_ECHO;
  } else {
    return CMD_INVALID;
  }
}