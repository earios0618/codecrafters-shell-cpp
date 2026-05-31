#include <unordered_map>

extern unordered_map<std::string, std::string> customCompletions; //storage for custom completions for complete command
extern int pipefd[2]; //pipe for custom completion, pipefd[0] is read end, pipefd[1] is write end

char** attempt_cmpltn(const char* text, int start, int end);
char* first_cmpltn(const char* text, int state);
char* file_cmpltn(const char* text, int state);
char *custom_cmpltn(const char* text, int state);