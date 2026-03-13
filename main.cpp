#include <iostream>
#include <cstring>
#include <fstream>
#include <vector>
#include <algorithm>

using namespace std;

const int M = 50; // Reduced B+ tree order
const int MAX_KEY_SIZE = 65;
const char* INDEX_FILE = "bptree.idx";

struct Key {
    char str[MAX_KEY_SIZE];

    Key() { memset(str, 0, sizeof(str)); }

    Key(const char* s) {
        memset(str, 0, sizeof(str));
        strncpy(str, s, MAX_KEY_SIZE - 1);
    }

    bool operator<(const Key& other) const {
        return strcmp(str, other.str) < 0;
    }

    bool operator==(const Key& other) const {
        return strcmp(str, other.str) == 0;
    }

    bool operator<=(const Key& other) const {
        return strcmp(str, other.str) <= 0;
    }
};

struct Record {
    Key key;
    int value;

    bool operator<(const Record& other) const {
        if (key == other.key) return value < other.value;
        return key < other.key;
    }
};

struct Node {
    bool isLeaf;
    int numKeys;
    Key keys[M];
    int children[M + 1];
    Record records[M];
    int next;

    Node() : isLeaf(true), numKeys(0), next(-1) {
        for (int i = 0; i <= M; i++) children[i] = -1;
    }
};

class BPlusTree {
private:
    fstream indexFile;
    int root;
    int nodeCount;

    void writeNode(int pos, const Node& node) {
        indexFile.seekp(sizeof(int) * 2 + (long long)pos * sizeof(Node));
        indexFile.write((char*)&node, sizeof(Node));
        indexFile.flush();
    }

    void readNode(int pos, Node& node) {
        indexFile.seekg(sizeof(int) * 2 + (long long)pos * sizeof(Node));
        indexFile.read((char*)&node, sizeof(Node));
    }

    void writeHeader() {
        indexFile.seekp(0);
        indexFile.write((char*)&root, sizeof(int));
        indexFile.write((char*)&nodeCount, sizeof(int));
        indexFile.flush();
    }

    void readHeader() {
        indexFile.seekg(0);
        indexFile.read((char*)&root, sizeof(int));
        indexFile.read((char*)&nodeCount, sizeof(int));
    }

    int findChild(Node& node, const Key& key) {
        int i = 0;
        while (i < node.numKeys && !(key < node.keys[i])) {
            i++;
        }
        return i;
    }

    bool insertIntoLeaf(Node& leaf, const Record& record) {
        // Check for duplicate
        for (int i = 0; i < leaf.numKeys; i++) {
            if (leaf.records[i].key == record.key && leaf.records[i].value == record.value) {
                return false; // Duplicate, don't insert
            }
        }

        int i = leaf.numKeys - 1;
        while (i >= 0 && record < leaf.records[i]) {
            leaf.records[i + 1] = leaf.records[i];
            i--;
        }
        leaf.records[i + 1] = record;
        leaf.numKeys++;
        return true;
    }

    int splitLeaf(int leafPos, Node& leaf, const Record& record, bool& inserted) {
        Node newLeaf;
        newLeaf.isLeaf = true;
        newLeaf.next = leaf.next;

        Record temp[M + 1];
        for (int i = 0; i < leaf.numKeys; i++) {
            temp[i] = leaf.records[i];
        }

        // Check for duplicate
        for (int i = 0; i < leaf.numKeys; i++) {
            if (temp[i].key == record.key && temp[i].value == record.value) {
                inserted = false;
                return -1;
            }
        }

        int pos = leaf.numKeys;
        for (int i = leaf.numKeys - 1; i >= 0 && record < temp[i]; i--) {
            temp[i + 1] = temp[i];
            pos = i;
        }
        temp[pos] = record;
        inserted = true;

        int mid = (M + 1) / 2;
        leaf.numKeys = mid;
        for (int i = 0; i < mid; i++) {
            leaf.records[i] = temp[i];
        }

        newLeaf.numKeys = M + 1 - mid;
        for (int i = 0; i < newLeaf.numKeys; i++) {
            newLeaf.records[i] = temp[mid + i];
        }

        int newLeafPos = nodeCount++;
        leaf.next = newLeafPos;

        writeNode(leafPos, leaf);
        writeNode(newLeafPos, newLeaf);

        return newLeafPos;
    }

    bool insertInternal(int nodePos, const Record& record, int& newChildPos, Key& promotedKey) {
        Node node;
        readNode(nodePos, node);

        if (node.isLeaf) {
            if (node.numKeys < M) {
                bool ins = insertIntoLeaf(node, record);
                if (ins) {
                    writeNode(nodePos, node);
                }
                newChildPos = -1;
                return ins;
            } else {
                bool inserted;
                newChildPos = splitLeaf(nodePos, node, record, inserted);
                if (!inserted) {
                    return false;
                }
                Node newChild;
                readNode(newChildPos, newChild);
                promotedKey = newChild.records[0].key;
                return true;
            }
        } else {
            int childIdx = findChild(node, record.key);
            int newGrandchildPos;
            Key newKey;
            bool inserted = insertInternal(node.children[childIdx], record, newGrandchildPos, newKey);

            if (!inserted) {
                newChildPos = -1;
                return false;
            }

            if (newGrandchildPos == -1) {
                newChildPos = -1;
                return true;
            }

            if (node.numKeys < M) {
                int i = node.numKeys - 1;
                while (i >= 0 && newKey < node.keys[i]) {
                    node.keys[i + 1] = node.keys[i];
                    node.children[i + 2] = node.children[i + 1];
                    i--;
                }
                node.keys[i + 1] = newKey;
                node.children[i + 2] = newGrandchildPos;
                node.numKeys++;
                writeNode(nodePos, node);
                newChildPos = -1;
                return true;
            } else {
                Key tempKeys[M + 1];
                int tempChildren[M + 2];

                for (int i = 0; i < node.numKeys; i++) {
                    tempKeys[i] = node.keys[i];
                    tempChildren[i] = node.children[i];
                }
                tempChildren[node.numKeys] = node.children[node.numKeys];

                int pos = node.numKeys;
                for (int i = node.numKeys - 1; i >= 0 && newKey < tempKeys[i]; i--) {
                    tempKeys[i + 1] = tempKeys[i];
                    tempChildren[i + 2] = tempChildren[i + 1];
                    pos = i;
                }
                tempKeys[pos] = newKey;
                tempChildren[pos + 1] = newGrandchildPos;

                int mid = (M + 1) / 2;
                node.numKeys = mid;
                for (int i = 0; i < mid; i++) {
                    node.keys[i] = tempKeys[i];
                    node.children[i] = tempChildren[i];
                }
                node.children[mid] = tempChildren[mid];

                Node newNode;
                newNode.isLeaf = false;
                newNode.numKeys = M - mid;
                for (int i = 0; i < newNode.numKeys; i++) {
                    newNode.keys[i] = tempKeys[mid + 1 + i];
                    newNode.children[i] = tempChildren[mid + 1 + i];
                }
                newNode.children[newNode.numKeys] = tempChildren[M + 1];

                newChildPos = nodeCount++;
                writeNode(nodePos, node);
                writeNode(newChildPos, newNode);

                promotedKey = tempKeys[mid];
                return true;
            }
        }
    }

    bool deleteFromLeaf(Node& leaf, const Key& key, int value) {
        int i = 0;
        while (i < leaf.numKeys && (leaf.records[i].key < key ||
               (leaf.records[i].key == key && leaf.records[i].value < value))) {
            i++;
        }

        if (i >= leaf.numKeys || !(leaf.records[i].key == key) || leaf.records[i].value != value) {
            return false;
        }

        for (int j = i; j < leaf.numKeys - 1; j++) {
            leaf.records[j] = leaf.records[j + 1];
        }
        leaf.numKeys--;
        return true;
    }

    bool deleteInternal(int nodePos, const Key& key, int value) {
        Node node;
        readNode(nodePos, node);

        if (node.isLeaf) {
            bool found = deleteFromLeaf(node, key, value);
            if (found) {
                writeNode(nodePos, node);
            }
            return found;
        } else {
            int childIdx = findChild(node, key);
            return deleteInternal(node.children[childIdx], key, value);
        }
    }

    void findInLeaf(Node& leaf, const Key& key, vector<int>& results) {
        for (int i = 0; i < leaf.numKeys; i++) {
            if (leaf.records[i].key == key) {
                results.push_back(leaf.records[i].value);
            }
        }
    }

    void findInternal(int nodePos, const Key& key, vector<int>& results) {
        if (nodePos == -1) return;

        Node node;
        readNode(nodePos, node);

        if (node.isLeaf) {
            findInLeaf(node, key, results);
        } else {
            int childIdx = findChild(node, key);
            findInternal(node.children[childIdx], key, results);
        }
    }

public:
    BPlusTree() {
        ifstream checkFile(INDEX_FILE);
        if (checkFile.good()) {
            checkFile.close();
            indexFile.open(INDEX_FILE, ios::in | ios::out | ios::binary);
            if (indexFile.good()) {
                readHeader();
                return;
            }
            indexFile.close();
        }

        indexFile.open(INDEX_FILE, ios::out | ios::binary);
        indexFile.close();
        indexFile.open(INDEX_FILE, ios::in | ios::out | ios::binary);

        root = 0;
        nodeCount = 1;
        Node rootNode;
        rootNode.isLeaf = true;
        writeNode(root, rootNode);
        writeHeader();
    }

    ~BPlusTree() {
        if (indexFile.is_open()) {
            indexFile.close();
        }
    }

    void insert(const char* keyStr, int value) {
        Record record;
        record.key = Key(keyStr);
        record.value = value;

        int newChildPos;
        Key promotedKey;
        bool inserted = insertInternal(root, record, newChildPos, promotedKey);

        if (inserted && newChildPos != -1) {
            Node newRoot;
            newRoot.isLeaf = false;
            newRoot.numKeys = 1;
            newRoot.keys[0] = promotedKey;
            newRoot.children[0] = root;
            newRoot.children[1] = newChildPos;

            root = nodeCount++;
            writeNode(root, newRoot);
        }

        if (inserted) {
            writeHeader();
        }
    }

    void remove(const char* keyStr, int value) {
        Key key(keyStr);
        deleteInternal(root, key, value);
    }

    vector<int> find(const char* keyStr) {
        Key key(keyStr);
        vector<int> results;
        findInternal(root, key, results);
        sort(results.begin(), results.end());
        return results;
    }
};

int main() {
    ios::sync_with_stdio(false);
    cin.tie(0);

    BPlusTree tree;

    int n;
    cin >> n;

    for (int i = 0; i < n; i++) {
        string cmd;
        cin >> cmd;

        if (cmd == "insert") {
            string key;
            int value;
            cin >> key >> value;
            tree.insert(key.c_str(), value);
        } else if (cmd == "delete") {
            string key;
            int value;
            cin >> key >> value;
            tree.remove(key.c_str(), value);
        } else if (cmd == "find") {
            string key;
            cin >> key;
            vector<int> results = tree.find(key.c_str());

            if (results.empty()) {
                cout << "null\n";
            } else {
                for (size_t j = 0; j < results.size(); j++) {
                    if (j > 0) cout << " ";
                    cout << results[j];
                }
                cout << "\n";
            }
        }
    }

    return 0;
}
