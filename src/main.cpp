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
#include <cctype>

#include "commands.h"
#include "Trie.h"
#include <unordered_map>

std::string find_executable(std::string name);
void redirect_output(std::stringstream& commandStream, int replacingFD, const char mode[]);
char** attempt_cmpltn(const char* text, int start, int end);
char* first_cmpltn(const char* text, int state);
char* file_cmpltn(const char* text, int state);
char *custom_cmpltn(const char* text, int state);
void handle_builtin(Command command, std::vector<std::string>& args);
void handle_jobs(bool showRunning);
void parse_args(std::string& commandStream, std::vector<std::string>& args);
void handle_bckgrnd(std::vector<std::string>& args);
void handle_exec(std::vector<string>& args);
void handle_pipe(std::vector<std::string>& args, std::vector<std::string>::iterator& pipeIndex);
void parse_var(std::string& arg, int pos);

int originalStdout = dup(STDOUT_FILENO);
int originalStderr = dup(STDERR_FILENO);
int originalStdin = dup(STDIN_FILENO);
bool keepRunning = true;
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
int histAppended = 0;
std::unordered_map<std::string, std::string> shellVars;


//TODO: fix extra space in file auto completion double tab list, fix hidden files
int main() {
  // Flush after every std::cout / std:cerr
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;
  //auto completion using readline and Trie
  rl_attempted_completion_function = attempt_cmpltn;
  using_history();
  read_history(std::getenv("HISTFILE"));
  histAppended = history_length;
  while (keepRunning) {
    //restore stdout and stderr
    dup2(originalStdout, STDOUT_FILENO);
    dup2(originalStderr, STDERR_FILENO);
    handle_jobs(false); //check for completed jobs
    std::string commandLine;
    commandLine = readline("$ ");
    add_history(commandLine.c_str());
    //populate arguments, first argument is command
    std::vector<std::string> args;
    parse_args(commandLine, args);

    auto pipeIndex = std::find(args.begin(), args.end(), "|");
    //if '|' in command line, pipeline
    if (pipeIndex != args.end()){
      handle_pipe(args, pipeIndex);
      continue;
    }
    //background commands
    if (args.back() == "&") {
      args.pop_back();
      handle_bckgrnd(args);
      continue;
    }
    //normal command execution
    Command command = parse_command(args[0]);
    if (command == NOT_BUILTIN) {
      int pid = fork();
      if (pid == 0) {
        handle_exec(args);
      } else if (pid == -1) {
        std::cerr << "Failed to fork process\n";
      }
      waitpid(pid, nullptr, 0);
    } else {
      handle_builtin(command, args);
    }
  }
  close(originalStdout);
  close(originalStderr);
  close(originalStdin);
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
  return "";
}

//redirect replacingFD to fileName
void redirect_output(std::string fileName, int replacingFD, const char mode[]) {
  std::FILE* file = std::fopen(fileName.c_str(), mode);
  int fd = fileno(file);
  dup2(fd, replacingFD);
  close(fd);
}

char** attempt_cmpltn(const char* text, int start, int end) {
  rl_attempted_completion_over = 1;
  std::stringstream ss(rl_line_buffer);
  std::vector<std::string> tokens;
  std::string token;
  while (ss >> token) {
    tokens.push_back(token);
  }
  //no custom completion
  if (customCompletions.find(tokens[0]) == customCompletions.end()) {
    if (start == 0 || tokens[0] == "type") { //only do command completion for first word or after type command
      rl_completion_append_character = ' ';
      return rl_completion_matches(text, first_cmpltn);
    } else {
      rl_completion_append_character = '\0';
      return rl_completion_matches(text, file_cmpltn);
    }
  }
  //custom completion
  pipe(pipefd);
  int pid = fork();
  if (pid == 0) {
    // Execute the custom completion command
    close(pipefd[0]); // Close read end in child
    dup2(pipefd[1], STDOUT_FILENO); // Redirect stdout to pipe
    close(pipefd[1]); // Close original write end
    std::string fourthArg = tokens.size() > 1 ? tokens.at(tokens.size() - 2).c_str() : "";
    const char *args[5] = { customCompletions[tokens[0]].c_str(), tokens[0].c_str(), tokens.back().c_str(), 
                                              fourthArg.c_str(), nullptr };
    setenv("COMP_LINE", rl_line_buffer, 1);
    std::string compPointStr = std::to_string(rl_point);
    setenv("COMP_POINT", compPointStr.c_str(), 1);
    execv(args[0], (char* const*) args);
  } else if (pid > 0) {
    //get the output of custom completion
    waitpid(pid, nullptr, 0);
    close(pipefd[1]); // Close write end in parent
    return rl_completion_matches(text, custom_cmpltn);
  } else {
    std::cerr << "Failed to fork process for custom completion\n";
  }
  return nullptr;
}

char *custom_cmpltn(const char* text, int state) {
  static std::vector<std::string> matches;
  static size_t matchIndex;
  if (state == 0) {
    matches.clear();
    matchIndex = 0;
    std::FILE* infile = fdopen(pipefd[0], "r");
    char buffer[256];
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
      if (!dir) continue;
      struct dirent* entry;
      while ((entry = readdir(dir)) != nullptr) {
        std::string fileName(entry->d_name);
        if (strncmp(fileName.c_str(), prefix.c_str(), prefix.size()) == 0 && 
                                        access((path + '/' + fileName).c_str(), X_OK) == 0) {
          matches.push_back(fileName);
        }
      }
      closedir(dir);
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
    if (!directory) {
      return nullptr;
    }
    struct dirent* entry;
    while ((entry = readdir(directory)) != nullptr) {
      std::string fileName(entry->d_name);
      if (fileName == "." || fileName == "..") continue;
      if (strncmp(fileName.c_str(), prefix.c_str(), prefix.size()) == 0) {
        std::string returnMatch = dir == "." ? fileName : dir + '/' + fileName;
        // Check if it's a directory or file
        returnMatch = entry->d_type == DT_DIR ? returnMatch + "/" : returnMatch + " ";
        matches.push_back(returnMatch);
      }
    }
    closedir(directory);
  }
  if (matchIndex < matches.size()) {
    return strdup(matches[matchIndex++].c_str());
  } else {
    return nullptr;
  }
}

void handle_builtin(Command command, std::vector<std::string>& args) {
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
    case CMD_EXIT:
      keepRunning = false;
      append_history(history_length - histAppended, std::getenv("HISTFILE"));
      break;
    case CMD_HIST: {
      int start = 1;
      if (args.size() > 1) {
        if (args[1] == "-r") {
          //add history from file
          read_history(args[2].c_str());
          break;
        } else if (args[1] == "-w") {
          //write history to file
          write_history(args[2].c_str());
          break;
        } else if (args[1] == "-a") {
          //append history to file
          append_history(history_length - histAppended, args[2].c_str());
          histAppended = history_length;
          break;
        }
        else {
          //show the last args[1] entries
          start = history_length - std::stoi(args[1]) + 1; //replace with faster impl, from_char
        }
      }
      for (int i = start; i <= history_length; i++) {
        std::cout << "\t" << i << " " << history_get(i)->line << std::endl;
      }
      break;
    }
    case CMD_DCLR:
      if (args[1] == "-p") { //display variable
        if (shellVars.find(args[2]) != shellVars.end()) {
          std::cout << "declare -- " << args[2] << "=\"" << shellVars[args[2]] << "\"" << endl;
        } else {
          std::cerr << "declare: " << args[2] << ": not found" << std::endl;
        }
      } else { //create variable
        auto pos = args[1].find("=");
        std::string varName(args[1], 0, pos);
        //check validity of variable name
        if (!std::isalpha(varName[0]) && varName[0] != '_') {
          std::cerr << "declare: `" << args[1] << "': not a valid identifier" << std::endl;
          break;
        }
        for (char c : varName) {
          if (!std::isalnum(c) && c != '_') {
            std::cerr << "declare: `" << args[1] << "': not a valid identifier" << std::endl;
            return;
          }
        }
        //assign value
        std::string varValue(args[1], pos + 1);
        shellVars[varName] = varValue;
      }
      break;
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
      job.status = "Running";
      if (!showRunning) continue;
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

void parse_args(std::string& commandLine, std::vector<std::string>& args) {
  std::string arg;
  bool inSingle = false; //in single quotes
  bool inDouble = false; //in double quotes
  bool takeLiteral = false; //set by escape character '/'
  for (int i = 0; i < commandLine.size(); i++) {
    char ch = commandLine[i];
    if (takeLiteral) {
      arg.push_back(ch);
      takeLiteral = false;
    } else if (ch == '"') {
      inDouble = !inDouble;
    } else if (inDouble) {
      arg.push_back(ch);
    } else if (ch == '\'') {
      inSingle = !inSingle;
    } else if (inSingle) {
      arg.push_back(ch);
    } else if (ch == '\\') {
      takeLiteral = true;
    } else if (ch == '$') {
      parse_var(commandLine, i);
      i--;
    } else if (ch != ' ') {
      arg.push_back(ch);
    } else if (!arg.empty()) {
      arg.erase(std::remove(arg.begin(), arg.end(), '"'), arg.end()); //TODO: work with quotes
      args.push_back(arg);
      arg.clear();
    }
  }
  if (!arg.empty()) {
    arg.erase(std::remove(arg.begin(), arg.end(), '"'), arg.end()); //TODO: work with quotes
    args.push_back(arg);
  }
  std::vector<int> toRemove;
  for (int i = 0; i < args.size(); i++) { //output redirection
    std::string arg = args[i];
    if ((arg == ">" || arg == "1>") && args.size() > 0) {
      redirect_output(args[i + 1], STDOUT_FILENO, "w");
    } else if(arg == "2>" && args.size() > 0) {
      redirect_output(args[i + 1], STDERR_FILENO, "w");
    } else if ((arg == ">>" || arg == "1>>") && args.size() > 0) {
      redirect_output(args[i + 1], STDOUT_FILENO, "a");
    } else if(arg == "2>>" && args.size() > 0) {
      redirect_output(args[i + 1], STDERR_FILENO, "a");
    } else {
      continue;
    }
    //set to remove from args
    toRemove.push_back(i);
    toRemove.push_back(++i);
  }
  std::sort(toRemove.rbegin(), toRemove.rend()); // reverse indicies
  for (int index : toRemove) { //remove arguments starting from later indicies to preserve validity
    args.erase(args.begin() + index);
  }
}

void handle_bckgrnd(std::vector<std::string>& args){
  int pid = fork();
  if (pid == 0) {
    Command command = parse_command(args[0]);
    if (command == NOT_BUILTIN) {
      handle_exec(args);
    } else {
      handle_builtin(command, args);
      exit(0);
    }
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
  } else {
    std::cerr << "Failed to fork process for background execution\n";
  } 
}

void handle_exec(std::vector<string>& args){
  std::string path = find_executable(args[0]);
  if (!path.empty()) {
    // Convert string arguments to c strings
    const char* argv[args.size() + 1];
    for (size_t i = 0; i < args.size(); i++) {
      argv[i] = args[i].c_str();
    }
    argv[args.size()] = nullptr; // Null-terminate the array
    // Execute the file
    execv(path.c_str(), (char* const*) argv);
  } else {
    std::cerr << args[0] << ": command not found\n";
    exit(1);
  }
}

void handle_pipe(std::vector<std::string>& args, std::vector<std::string>::iterator& pipeIndex) {
  //parse commands
  std::vector<std::vector<string>> commands;
  std::vector<std::string>::iterator start = args.begin();
  while (true) {
    std::vector<std::string>::iterator newStart = std::find(start, args.end(), "|");
    std::vector<string> command(start, newStart);
    commands.push_back(command);
    if (newStart == args.end()) {
      break;
    }
    start = newStart + 1;
  }

  std::vector<int> pids;
  int pipeRead; //previous pipe read fd
  //for every command
  for (int i = 0; i < commands.size(); i++) {
    //set up read pipe
    if (i != 0) {
      dup2(pipeRead, STDIN_FILENO);
      close(pipeRead);
    }
    //set up pipe write
    if (i != commands.size() - 1) {
      int pipes[2];
      pipe(pipes);
      pipeRead = pipes[0];
      dup2(pipes[1], STDOUT_FILENO);
      close(pipes[1]);
    } else {
      dup2(originalStdout, STDOUT_FILENO); //restore stdout
    }
    int pid = fork();
    if (pid == 0) {
      Command cmd = parse_command(commands[i][0]);
      if (cmd == NOT_BUILTIN) {
        handle_exec(commands[i]);
      } else {
        handle_builtin(cmd, commands[i]);
        exit(0);
      }
    } else if (pid == -1) {
      std::cerr << "Failed to fork process\n";
    }
    pids.push_back(pid);
  }
  dup2(originalStdin, STDIN_FILENO); //restore stdin, really not neccesary as this is end of commands
  //wait for every process
  for (int pid : pids) {
    waitpid(pid, nullptr, 0);
  }
}

//shell variable parsing, returns number of chars to skip when parsing
void parse_var(std::string& arg, int pos) {
  std::string varName;
  std::size_t varEnd;
  if (arg[pos + 1] == '{') {
    varEnd = arg.find('}', pos + 2);
    varName = arg.substr(pos + 2, varEnd - (pos + 2));
  } else {
    varEnd = arg.find(' ', pos + 1);
    if (varEnd == std::string::npos) {
      varEnd = arg.size();
    }
    varName = arg.substr(pos + 1, varEnd - (pos + 1));
    varEnd--;
  }
  std::string var;
  std::string argStart = arg.substr(0, pos);
  std::string argEnd = arg.substr(varEnd + 1);
  if (shellVars.find(varName) != shellVars.end()) {
    var = shellVars[varName];
  } else {
    var = ""; //replace with empty string if variable not found
  }
  arg = argStart + var + argEnd;
}