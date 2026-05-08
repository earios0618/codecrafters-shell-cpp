#include <iostream>
#include <string>
#include <commands.h>

int main() {
  // Flush after every std::cout / std:cerr
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;
  while (true) {
    std::cout << "$ ";
    std::string commandLine;
    std::getline(std::cin, commandLine);
    Command command = parse_command(commandLine);
    switch (command) {
      case CMD_EXIT:
        break;
      case CMD_ECHO:
        std::cout << commandLine.substr(5) << std::endl;
        break;
      case CMD_TYPE: {
        std::string parameter = commandLine.substr(5);
        Command subCommand = parse_command(parameter);
        if (subCommand == CMD_INVALID) {
          std::cout << parameter + ": not found\n";
        } else {
          std::cout << parameter + " is a shell builtin\n";
        }
      }
      default:
        std::cout << commandLine + ": command not found\n";
    }
  }
}

Command parse_command(std::string commandLine) {
  if (commandLine == "exit") {
    return CMD_EXIT;
  } else if (commandLine.substr(0, 4) == "echo") {
    return CMD_ECHO;
  } else {
    return CMD_INVALID;
  }
}