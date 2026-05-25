#include "commands.h"

//initialize the Trie with builtin commands for auto-completion
Trie init_cmd_trie() {
  Trie commandTrie;
  commandTrie.insert("echo");
  commandTrie.insert("exit");
  commandTrie.insert("type");
  commandTrie.insert("pwd");
  commandTrie.insert("cd");
  commandTrie.insert("complete");
  commandTrie.insert("jobs");
  commandTrie.insert("history");
  return commandTrie;
}

//return enum for string command
Command parse_command(std::string commandString) {
  if (commandString == "echo") { return CMD_ECHO; } 
  else if (commandString == "exit") { return CMD_EXIT; } 
  else if (commandString == "type") { return CMD_TYPE; } 
  else if (commandString == "pwd") { return CMD_PWD; } 
  else if (commandString == "cd") { return CMD_CD; } 
  else if (commandString == "complete") { return CMD_CMPLT; } 
  else if (commandString == "jobs") { return CMD_JOBS; } 
  else if (commandString == "history") { return CMD_HIST; } 
  else { return NOT_BUILTIN; }
}