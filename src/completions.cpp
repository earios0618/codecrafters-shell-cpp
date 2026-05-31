#include <iostream>
#include <sstream>
#include <sys/wait.h>
#include <readline/readline.h>
#include <dirent.h>

#include "commands.h"
#include "completions.h"

int pipefd[2];

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