#pragma once
#include <string>
#include <vector>
using namespace std;

// Trie data structure for storing lowercase English words, used for command lookup
class Trie {
    class TrieNode {
    public:
        TrieNode *children[26];
        char ch;
        bool word;
        
        TrieNode(char character, bool word) {
            for (int i = 0; i < 26; i++) {
                children[i] = nullptr;
            }
            this->ch = character;
            this->word = word;
        }

        ~TrieNode(){
            for (int i = 0; i < 26; i++) {
                delete children[i];
            }
        }

        TrieNode* get_TrieNode(char ch) {
            int index = ch - 'a';
            if (children[index]) {
                return this->children[index];
            }
            TrieNode* newTrieNode = new TrieNode(ch, false);
            this->children[index] = newTrieNode;
            return newTrieNode;
        }

        TrieNode* find_TrieNode(char ch) {
            int index = ch - 'a';
            if (children[index]) {
                return this->children[index];
            }
            return nullptr;
        }
    };//end of TrieTrieNode class

    TrieNode *root;
    void prefixHelper(TrieNode* node, string prefix, vector<string>& words) {
        for (int i = 0; i < 26; i++) {
            if (node->children[i]) {
                prefixHelper(node->children[i], prefix + node->children[i]->ch, words);
            }
        }
        if (node->word) {
            words.push_back(prefix);
        }
    }
public:
    Trie(){
        root = new TrieNode('\0', false);
    }

    ~Trie(){}
    
    void insert(string word) {
        TrieNode *curr = root;
        for (int i = 0; i < word.size(); i++) {
            curr = curr->get_TrieNode(word[i]);
        }
        curr->word = true;
    }
    
    bool search(string word) {
        TrieNode *curr = root;
        for (int i = 0; i < word.size(); i++) {
            curr = curr->find_TrieNode(word[i]);
            if (!curr) {
                return false;
            }
        }
        return curr->word;
    }
    
    bool startsWith(string prefix) {
        TrieNode *curr = root;
        for (int i = 0; i < prefix.size(); i++) {
            curr = curr->find_TrieNode(prefix[i]);
            if (!curr) {
                return false;
            }
        }
        return true;
    }

    vector<string> withPrefix(string prefix) {
        TrieNode *curr = root;
        for (int i = 0; i < prefix.size(); i++) {
            curr = curr->find_TrieNode(prefix[i]);
            if (!curr) {
                return {};
            }
        }
        vector<string> words;
        prefixHelper(curr, prefix, words);
        return words;
    }

};