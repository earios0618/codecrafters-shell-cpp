#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <algorithm>
#include <unistd.h>
#include <sys/wait.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <dirent.h>
#include <iomanip>
#include <set>

#include "commands.h"
#include "Trie.h"
#include <unordered_map>



Command parse_command(std::string commandLine);
std::string find_executable(std::string name);
void redirect_output(std::stringstream& commandStream, int replacingFD, const char mode[]);
Trie init_cmd_trie();
char** attempt_cmpltn(const char* text, int start, int end);
char* first_cmpltn(const char* text, int state);
char* file_cmpltn(const char* text, int state);
int originalStdout = dup(STDOUT_FILENO);
int originalStderr = dup(STDERR_FILENO);
char *custom_cmpltn(const char* text, int state);
void handle_cmd(Command command, std::vector<std::string>& args);
void handle_jobs(bool showRunning);

Trie commandTrie = init_cmd_trie();
unordered_map<std::string, std::string> customCompletions; //storage for custom completions for complete command
int pipefd[2]; //pipe for custom completion, pipefd[0] is read end, pipefd[1] is write end
int bckgrndJobs = 0; //number of background jobs called
struct Job {
  int id;
  pid_t pid;
  std::string commandLine;
  std::string status;
};
std::vector<Job> jobs; //storage for background jobs
std::set<int> availIDs; //storage for available job IDs

//TODO: make exit case more like others using direct call to exit(), fix extra space in file auto completion double tab list
int main() {
  // Flush after every std::cout / std:cerr
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;
  //auto completion using readline and Trie
  rl_attempted_completion_function = attempt_cmpltn;
  while (true) {
    //restore stdout and stderr
    dup2(originalStdout, STDOUT_FILENO);
    dup2(originalStderr, STDERR_FILENO);
    handle_jobs(false); //check for completed jobs
    std::string commandLine;
    commandLine = readline("$ ");
    std::stringstream commandStream(commandLine);
    //populate arguments, first argument is command
    std::vector<std::string> args;
    std::string arg;
    while (commandStream >> arg) {
      if ((arg == ">" || arg == "1>") && args.size() > 0) {
        redirect_output(commandStream, STDOUT_FILENO, "w");
        continue;
      } else if(arg == "2>" && args.size() > 0) {
        redirect_output(commandStream, STDERR_FILENO, "w");
        continue;
      } else if ((arg == ">>" || arg == "1>>") && args.size() > 0) {
        redirect_output(commandStream, STDOUT_FILENO, "a");
        continue;
      } else if(arg == "2>>" && args.size() > 0) {
        redirect_output(commandStream, STDERR_FILENO, "a");
        continue;
      }
      args.push_back(arg);
    }
    if (args.back() == "&") {
      args.pop_back();
      int pid = fork();
      if (pid == 0) {
        Command command = parse_command(args[0]);
        if (command != NOT_BUILTIN) {
          handle_cmd(command, args);
          exit(0);
        }
        std::string path = find_executable(args[0]);
        if (path.empty()) {
          std::cerr << args[0] << ": command not found" << std::endl;
          exit(1);
        }
        // Convert string arguments to c strings
        const char* argv[args.size() + 1];
        for (size_t i = 0; i < args.size(); i++) {
          argv[i] = args[i].c_str();
        } 
        argv[args.size()] = nullptr; // Null-terminate the array
        execv(path.c_str(), (char* const*) argv);
      } else if (pid > 0) {
        // Parent process does not wait for the child and continues to the next iteration of the loop
        std::string fullCommandLine;
        for (const std::string& arg : args) {
          fullCommandLine += arg + " ";
        }
        fullCommandLine.pop_back(); // Remove trailing space
        int jobID;
        if (!availIDs.empty()) {
          jobID = *availIDs.begin();
          availIDs.erase(availIDs.begin());
        } else {         
          jobID = ++bckgrndJobs;
        }
        Job newJob = {jobID, pid, fullCommandLine, ""}; //string is empty becasue current status is unknown
        jobs.push_back(newJob);
        std::cout << "[" << jobID << "] " << pid << std::endl;
        continue;
      } else {
        std::cerr << "Failed to fork process for background execution\n";
      }
    }
    Command command = parse_command(args[0]);
    //special command case
    if (command == CMD_EXIT) {
      break;
    }
    handle_cmd(command, args);
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
  } else if (commandString == "complete") {
    return CMD_CMPLT;
  } else if (commandString == "jobs") {
    return CMD_JOBS;
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

//redirect replacingFD to next argument in stream which should be a file
void redirect_output(std::stringstream& commandStream, int replacingFD, const char mode[]) {
  std::string fileName;
  commandStream >> fileName;
  std::FILE* file = std::fopen(fileName.c_str(), mode);
  int fd = fileno(file);
  dup2(fd, replacingFD);
  close(fd);
}

//initialize the Trie with builtin commands for auto-completion
Trie init_cmd_trie() {
  Trie commandTrie;
  commandTrie.insert("echo");
  commandTrie.insert("exit");
  commandTrie.insert("type");
  commandTrie.insert("pwd");
  commandTrie.insert("cd");
  commandTrie.insert("complete");
  return commandTrie;
}

char** attempt_cmpltn(const char* text, int start, int end) {
  rl_attempted_completion_over = 1;
  std::stringstream ss(rl_line_buffer);
  std::vector<std::string> tokens;
  std::string token;
  while (ss >> token) {
    tokens.push_back(token);
  }
  if (customCompletions.find(tokens[0]) != customCompletions.end()) {
    pipe(pipefd);
    int pid = fork();
    if (pid == 0) {
      // Execute the custom completion command and capture its output
      close(pipefd[0]); // Close read end in child
      dup2(pipefd[1], STDOUT_FILENO); // Redirect stdout to pipe
      close(pipefd[1]); // Close original write end
      const char *args[5];
      std::string fourthArg = tokens.size() > 1 ? tokens.at(tokens.size() - 2).c_str() : "";
      args[0] = customCompletions[tokens[0]].c_str();
      args[1] = tokens[0].c_str();
      args[2] = tokens.back().c_str();
      args[3] = fourthArg.c_str();
      args[4] = nullptr;
      setenv("COMP_LINE", rl_line_buffer, 1);
      std::string compPointStr = std::to_string(rl_point);
      setenv("COMP_POINT", compPointStr.c_str(), 1);
      execv(customCompletions[tokens[0]].c_str(), (char* const*) args);
    } else if (pid > 0) {
      waitpid(pid, nullptr, 0);
      close(pipefd[1]); // Close write end in parent
      return rl_completion_matches(text, custom_cmpltn);
    } else {
      std::cerr << "Failed to fork process for custom completion\n";
    }
  }
  if (start == 0 || tokens[0] == "type") { //only do command completion for first word or after type command
    rl_completion_append_character = ' ';
    return rl_completion_matches(text, first_cmpltn);
  } else {
    rl_completion_append_character = '\0';
    return rl_completion_matches(text, file_cmpltn);
  }
}

char *custom_cmpltn(const char* text, int state) {
  static std::vector<std::string> matches;
  static size_t matchIndex;
  if (state == 0) {
    matches.clear();
    matchIndex = 0;
    std::FILE* infile = fdopen(pipefd[0], "r");
    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), infile)) {
      std::string line(buffer);
      line.pop_back(); // Remove newline character
      matches.push_back(line);
    }
    fclose(infile);
    close(pipefd[0]);
  }
  if (matchIndex < matches.size()) {
    return strdup(matches[matchIndex++].c_str());
  } else {
    return nullptr;
  }
}

//auto completion for commands and executables in PATH
char* first_cmpltn(const char* text, int state) {
  static std::vector<std::string> matches;
  static size_t matchIndex;

  if (state == 0) {
    matches.clear();
    matchIndex = 0;
    std::string prefix(text);
    if (prefix.empty()) {
      return nullptr;
    }
    matches = commandTrie.withPrefix(prefix); //add command matches
    //add executable matches from PATH
    std::string path_env = std::getenv("PATH");
    std::stringstream ss_path(path_env);
    std::string path;
    while (std::getline(ss_path, path, ':')) {
      DIR* dir = opendir(path.c_str());
      if (dir) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
          std::string fileName(entry->d_name);
          if (strncmp(fileName.c_str(), prefix.c_str(), prefix.size()) == 0) {
            if (access((path + '/' + fileName).c_str(), X_OK) == 0) {
              matches.push_back(fileName);
            }
          }
        }
        closedir(dir);
      }
    }
  }

  if (matchIndex < matches.size()) {
    return strdup(matches[matchIndex++].c_str());
  } else {
    return nullptr;
  }
}

char* file_cmpltn(const char* text, int state) {
  static std::vector<std::string> matches;
  static size_t matchIndex;
  if (state == 0) {
    matches.clear();
    matchIndex = 0;
    std::string dir = ".";
    std::string prefix(text);
    size_t lastSlash = prefix.find_last_of('/');
    if (lastSlash != std::string::npos) {
      dir = prefix.substr(0, lastSlash);
      prefix = prefix.substr(lastSlash + 1);
    }
    DIR* directory = opendir(dir.c_str());
    if (directory) {
      struct dirent* entry;
      while ((entry = readdir(directory)) != nullptr) {
        std::string fileName(entry->d_name);
        if (fileName == "." || fileName == "..") continue;
        if (strncmp(fileName.c_str(), prefix.c_str(), prefix.size()) == 0) {
          std::string fullPath = dir + '/' + fileName;
          std::string returnMatch = dir == "." ? fileName : fullPath;
          // Check if it's a directory or file
          if (entry->d_type == DT_DIR) {
            matches.push_back(returnMatch + "/"); // Append '/' for directories
          } else {
            matches.push_back(returnMatch + " "); // Append ' ' for files
          }
        }
      }
      closedir(directory);
    }
  }
  if (matchIndex < matches.size()) {
    return strdup(matches[matchIndex++].c_str());
  } else {
    return nullptr;
  }
}

void handle_cmd(Command command, std::vector<std::string>& args) {
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
        const char* argv[args.size() + 1];
        for (size_t i = 0; i < args.size(); i++) {
          argv[i] = args[i].c_str();
        }
        argv[args.size()] = nullptr; // Null-terminate the array
        // Execute the file
        pid_t pid = fork();
        if (pid == 0) {
          execv(path.c_str(), (char* const*) argv);
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
    case CMD_CMPLT: {
      if (args[1] == "-p") {
        if (customCompletions.find(args[2]) != customCompletions.end()) {
          std::cout << "complete -C '" << customCompletions[args[2]] << "' " << args[2] << std::endl;
        } else {
          std::cerr << "complete: " << args[2] << ": no completion specification" << std::endl;
        }
      } else if (args[1] == "-C") {
        customCompletions[args[3]] = args[2];
      } else if (args[1] == "-r") {
        customCompletions.erase(args[2]);
      } else {
        std::cerr << "complete: invalid option " << args[1] << std::endl;
      }
      break;
    }
    case CMD_JOBS: {
      handle_jobs(true);
      break;
    }
    default:
      std::cerr << args[0] << ": command not found\n";
      break;
  }
}

void handle_jobs(bool showRunning) {
  // print job list, remove done jobs from list, update status of running jobs
  for (int i = 0; i < jobs.size(); i++) {
    Job& job = jobs[i];
    if (waitpid(job.pid, nullptr, WNOHANG) == 0) {
      if (!showRunning) continue;
      job.status = "Running";
    } else {
      job.status = "Done";
      availIDs.insert(job.id);
    }
    std::string specialMarker = (i == jobs.size() - 1) ? "+ " : (i == jobs.size() - 2) ? "- " : "  ";
    std::cout << "[" << job.id << "] " << specialMarker << std::left << std::setw(24) << job.status
        << std::right << job.commandLine << std::endl;
  }
  jobs.erase(
    std::remove_if(jobs.begin(), jobs.end(), [](Job& job) {
      return job.status == "Done"; 
    }), 
    jobs.end()
  );    
}