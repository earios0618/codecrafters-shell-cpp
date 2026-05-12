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
std::string find_executable(std::string name);
void redirect_output(std::string fileName, int replacingFD);
//change error messages to error stream, get all arguments from the get go all while checking for quotes or >>
//make exit case more like others using bool
int main() {
  // Flush after every std::cout / std:cerr
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;
  int originalStdout = dup(STDOUT_FILENO);
  int originalStderr = dup(STDERR_FILENO);
  while (true) {
    std::cout << "$ ";
    std::string commandLine;
    //commandLine is just faux variable
    std::getline(std::cin, commandLine);
    std::stringstream commandStream(commandLine);
    //populate arguments, first argument is command
    std::vector<std::string> args;
    std::string arg;
    while (commandStream >> arg) {
      if (arg == ">" || arg == "1>" && args.size() > 0) {
        //char output var to commandStream >>, check if file is exists, if not create it
        std::string fileName;
        commandStream >> fileName;
        redirect_output(fileName, STDOUT_FILENO);
        continue;
      } else if(arg == "2>" && args.size() > 0) {
        std::string fileName;
        commandStream >> fileName;
        redirect_output(fileName, STDERR_FILENO);
        continue;
      }
      args.push_back(arg);
    }
    Command command = parse_command(args[0]);
    //special command case
    if (command == CMD_EXIT) {
      break;
    }
    switch (command) {
      case CMD_ECHO:{
        for (size_t i = 1; i < args.size(); i++) {
          std::cout << args[i];
          if (i < args.size() - 1) {
            std::cout << " ";
          }
        }
        std::cout << std::endl;
        break;
      }
      case CMD_TYPE: {
        Command subCommand = parse_command(args[1]);
        if (subCommand == NOT_BUILTIN) {
          std::string path = find_executable(args[1]);
          if (!path.empty()) {
            std::cout << args[1] << " is " << path << std::endl;
          } else {
            std::cout << args[1] << ": not found\n";
          }
        } else {
          std::cout << args[1] << " is a shell builtin\n";
        }
        break;
      }
      case NOT_BUILTIN: {
        std::string path = find_executable(args[0]);
        if (!path.empty()) {
          // Convert string arguments to c strings
          std::vector<const char*> argv;
          for (const auto& s : args) {
            argv.push_back(s.c_str());
          }
          argv.push_back(nullptr); // Null-terminate the vector
          // Execute the file
          pid_t pid = fork();
          if (pid == 0) {
            execv(path.c_str(), (char* const*) argv.data());
          } else if (pid > 0) {
            waitpid(pid, nullptr, 0);
          } else {
            std::cerr << "Failed to fork process\n";
          }
        } else {
          std::cerr << args[0] << ": command not found\n";
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
      case CMD_CD: {
        std::string path = args[1] != "~" ? args[1] : std::getenv("HOME");
        if (chdir(path.c_str()) != 0) {
          std::cerr << "cd: " << path << ": No such file or directory\n";
        }
        break;
      }
      default:
        std::cerr << args[0] << ": command not found\n";
        break;
    }
    //restore stdout and stderr
    dup2(originalStdout, STDOUT_FILENO);
    dup2(originalStderr, STDERR_FILENO);
  }
  close(originalStdout);
  close(originalStderr);
}

//return enum for string command
Command parse_command(std::string commandString) {
  if (commandString == "exit") {
    return CMD_EXIT;
  } else if (commandString == "type"){
    return CMD_TYPE;
  } else if (commandString == "echo") {
    return CMD_ECHO;
  } else if (commandString == "pwd") {
    return CMD_PWD;
  } else if (commandString == "cd") {
    return CMD_CD;
  } else {
    return NOT_BUILTIN;
  }
}

//search through PATH environment variable for executable, return path if found, empty string if not found
std::string find_executable(std::string name) {
  std::string path_env = std::getenv("PATH");
  std::stringstream ss_path(path_env);
  std::string path;
  //specific semantics to linux
  while (std::getline(ss_path, path, ':')) {
    std::string full_path = path + '/' + name;
    if (access(full_path.c_str(), X_OK) == 0) {
      return full_path;
    }
  }
  return ""; // Return empty string if not found
}

//redirect replacingFD to fileName fd
void redirect_output(std::string fileName, int replacingFD) {
  std::FILE* file = std::fopen(fileName.c_str(), "w");
  int fd = fileno(file);
  dup2(fd, replacingFD);
  close(fd);
}