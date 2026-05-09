#include <iostream>
#include <string>
#include <sstream>
// #include <vector>
// #include <cstdlib>
// #include <process.h>
#include <Windows.h>

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
//refactor to use stream logic
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
        if (subCommand == NOT_BUILTIN) {
          std::string path_env = std::getenv("PATH");
          std::stringstream ss_path(path_env);
          std::string path;
          bool found = false;
          //maybe change this part due to os semantics
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
          std::stringstream commandStream(commandLine);
          std::string commandString;
          commandStream >> commandString;
          bool found = false;
          while (std::getline(ss_path, path, ':')) {
            std::string full_path = path + '/' + commandString;
            if (access(full_path.c_str(), X_OK) == 0) {
              // Execute the command
              std::string args = commandLine.substr(commandString.length());
              std::string new_cmd = full_path + args;
              STARTUPINFO si = { sizeof(STARTUPINFO) };
              si.dwFlags = STARTF_USESHOWWINDOW;
              si.wShowWindow = SW_HIDE;
              PROCESS_INFORMATION pi;
              if (CreateProcess(NULL, (LPWSTR)new_cmd.c_str(), NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
                WaitForSingleObject(pi.hProcess, INFINITE);
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
                found = true;
              }
              break;
            }
          }
          if (!found) {
            std::cout << commandString + ": command not found\n";
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
    return NOT_BUILTIN;
  }
}