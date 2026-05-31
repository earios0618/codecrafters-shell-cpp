#include "Trie.h"

enum Command {
    CMD_ECHO,
    CMD_EXIT,
    CMD_TYPE,
    CMD_PWD,
    CMD_CD,
    CMD_CMPLT,
    CMD_JOBS,
    CMD_HIST,
    CMD_DCLR,
    NOT_BUILTIN
};

extern Trie commandTrie;

Trie init_cmd_trie();
Command parse_command(std::string commandString);