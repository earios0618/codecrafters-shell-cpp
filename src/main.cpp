#include <iostream>
#include <string>
#include <sstream>
#include <vector>
// #include <cstdlib>
// #include <process.h>
#include <unistd.h>
#include <sys/wait.h>

#include "commands.h"


Command parse_command(std::string commandLine);
//refactor to use stream logic
int main() {
  // Flush after every std::cout / std:cerr
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;
  while (true) {
    std::cout << "$ ";
    std::string commandLine;
    //commandLine is just faux variable
    std::getline(std::cin, commandLine);
    std::stringstream commandStream(commandLine);
    std::string commandString;
    commandStream >> commandString;
    //skip whitespace
    commandStream >> std::ws;
    Command command = parse_command(commandString);
    //special command case
    if (command == CMD_EXIT) {
      break;
    }
    switch (command) {
      case CMD_ECHO:{
        std::string rest;
        std::getline(commandStream, rest);
        std::cout << rest << std::endl;
        break;
      }
      case CMD_TYPE: {
        std::string parameter;
        commandStream >> parameter;
        Command subCommand = parse_command(parameter);
        if (subCommand == NOT_BUILTIN) {
          std::string path_env = std::getenv("PATH");
          std::stringstream ss_path(path_env);
          std::string path;
          bool found = false;
          //specific semantics to linux
          while (std::getline(ss_path, path, ':')) {
            std::string full_path = path + '/' + parameter;
            if (access(full_path.c_str(), X_OK) == 0) {
              std::cout << parameter << " is " << full_path << std::endl;
              found = true;
              break;
            }
          }
          if (!found) {
            std::cout << parameter + ": not found\n";
          }
        } else {
          std::cout << parameter + " is a shell builtin\n";
        }
        break;
      }
      case NOT_BUILTIN: {
          std::string path_env = std::getenv("PATH");
          std::stringstream ss_path(path_env);
          std::string path;
          bool found = false;
          while (std::getline(ss_path, path, ':')) {
            std::string full_path = path + '/' + commandString;
            if (access(full_path.c_str(), X_OK) == 0) {
              found = true;
              //populate arguments
              std::vector<std::string> args;
              args.push_back(commandString);
              std::string arg;
              while (commandStream >> arg) {
                args.push_back(arg);
              }
              // Convert std::vector<std::string> to char* const*
              std::vector<const char*> argv;
              for (const auto& s : args) {
                argv.push_back(s.c_str());
              }
              argv.push_back(nullptr); // Null-terminate the array
              // Execute the file
              pid_t pid = fork();
              if (pid == 0) {
                execv(full_path.c_str(), (char* const*) argv.data());
              } else if (pid > 0) {
                waitpid(pid, nullptr, 0);
              } else {
                std::cerr << "Failed to fork process\n";
              }
              break;
            }
          }
          if (!found) {
            std::cout << commandString + ": command not found\n";
          }
          break;
      }
      case CMD_PWD: {
        //size is arbitrary, but should be large enough for most paths
        char cwd[1024];
        if (getcwd(cwd, sizeof(cwd)) != nullptr) {
          std::cout << cwd << std::endl;
        } else {
          std::cerr << "Error getting current working directory\n";
        }
        break;
      }
      default:
        std::cout << commandLine + ": command not found\n";
        break;
    }
  }
}

Command parse_command(std::string commandString) {
  if (commandString == "exit") {
    return CMD_EXIT;
  } else if (commandString == "type"){
    return CMD_TYPE;
  } else if (commandString == "echo") {
    return CMD_ECHO;
  } else if (commandString == "pwd") {
    return CMD_PWD;
  } else {
    return NOT_BUILTIN;
  }
}