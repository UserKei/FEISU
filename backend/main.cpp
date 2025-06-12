#include <crow.h>
#include <iostream>
#include <vector>
#include <map>
#include <set>
#include <stack>
#include <sstream>
#include <algorithm>
#include <stdexcept>
#include <cctype>
#include <iomanip>
#include <queue>
#include <unordered_map>
#include <unordered_set>

// 添加 Windows 版本定义
#ifdef _WIN32
#define _WIN32_WINNT 0x0A00  // Windows 10/11
#endif

using namespace std;

// 文法产生式结构体
struct Production {
    string left;
    vector<string> right;

    // 修复ε产生式检测
    bool isEpsilon() const {
        if (right.empty()) return true;
        if (right.size() == 1 && right[0] == "ε") return true;
        return false;
    }
};

// LR(0)项目：产生式索引 + 点位置
struct Item {
    int prodIndex;    // 产生式索引
    int dotPos;       // 点的位置

    bool operator==(const Item& other) const {
        return prodIndex == other.prodIndex && dotPos == other.dotPos;
    }

    bool operator<(const Item& other) const {
        if (prodIndex != other.prodIndex) return prodIndex < other.prodIndex;
        return dotPos < other.dotPos;
    }
};

// 哈希函数特化
namespace std {
    template<>
    struct hash<Item> {
        size_t operator()(const Item& item) const {
            return hash<int>()(item.prodIndex) ^ (hash<int>()(item.dotPos) << 1);
        }
    };
}

// 语法分析器基类
class ParserBase {
public:
    // 文法组成部分
    set<string> nonTerminals;   // 非终结符集合
    set<string> terminals;      // 终结符集合（包含#）
    vector<Production> productions;  // 产生式列表
    string startSymbol;          // 开始符号

    // 扩展后的文法
    string augmentedStartSymbol; // 扩展后的开始符号（S'）
    int augmentedProductionIndex; // 扩展产生式的索引

    // LR(0)项目集族
    vector<set<Item>> itemSets;  // 项目集族

    // 分析表
    map<pair<int, string>, string> actionTable; // ACTION表
    map<pair<int, string>, int> gotoTable;      // GOTO表

    // FIRST集和FOLLOW集
    map<string, set<string>> firstSet;
    map<string, set<string>> followSet;

    // 分析过程步骤
    struct ParseStep {
        int step;
        string stateStack;
        string symbolStack;
        string currentInput;
        string remainingInput;
        string action;
    };

    vector<ParseStep> parseSteps; // 存储分析过程
    bool parseResult;             // 分析结果

    // 纯虚函数，由派生类实现
    virtual void buildParseTable() = 0;

    // 清理所有缓存数据
    virtual void clearCache() {
        nonTerminals.clear();
        terminals.clear();
        productions.clear();
        startSymbol.clear();
        augmentedStartSymbol.clear();
        augmentedProductionIndex = -1;
        
        itemSets.clear();
        actionTable.clear();
        gotoTable.clear();
        firstSet.clear();
        followSet.clear();
        parseSteps.clear();
        parseResult = false;
    }

    // 字符串分割函数
    vector<string> split(const string& s, char delimiter) {
        vector<string> tokens;
        string token;
        istringstream tokenStream(s);
        while (getline(tokenStream, token, delimiter)) {
            // 移除首尾空白字符
            token.erase(0, token.find_first_not_of(" \t"));
            token.erase(token.find_last_not_of(" \t") + 1);
            if (!token.empty()) tokens.push_back(token);
        }
        return tokens;
    }

    // 判断符号是否为终结符
    bool isTerminal(const string& symbol) {
        return terminals.count(symbol) || symbol == "#";
    }

    // 计算项目集闭包
    set<Item> closure(const set<Item>& items) {
        set<Item> closureSet = items;
        bool changed;
        do {
            changed = false;
            set<Item> newItems;

            // 遍历闭包中的每个项目
            for (const auto& item : closureSet) {
                const Production& prod = productions[item.prodIndex];

                // 如果点在末尾，跳过
                if (item.dotPos >= static_cast<int>(prod.right.size())) continue;

                string nextSymbol = prod.right[item.dotPos];

                // 如果下一个符号是非终结符
                if (nonTerminals.count(nextSymbol)) {
                    // 添加所有以该非终结符为左部的产生式
                    for (size_t i = 0; i < productions.size(); i++) {
                        if (productions[i].left == nextSymbol) {
                            Item newItem{ static_cast<int>(i), 0 }; // 点在开头
                            if (closureSet.find(newItem) == closureSet.end() &&
                                newItems.find(newItem) == newItems.end()) {
                                newItems.insert(newItem);
                                changed = true;
                            }
                        }
                    }
                }
            }

            // 添加新项目到闭包
            closureSet.insert(newItems.begin(), newItems.end());
        } while (changed);

        return closureSet;
    }

    // 计算转移函数
    set<Item> goTo(const set<Item>& items, const string& symbol) {
        set<Item> result;

        for (const auto& item : items) {
            const Production& prod = productions[item.prodIndex];

            // 如果点在末尾，跳过
            if (item.dotPos >= static_cast<int>(prod.right.size())) continue;

            // 如果当前符号匹配
            if (prod.right[item.dotPos] == symbol) {
                result.insert({ item.prodIndex, item.dotPos + 1 }); // 移动点
            }
        }

        return closure(result);
    }

    // 构建LR(0)项目集族
    void buildItemSets() {
        itemSets.clear();
        queue<int> unprocessedSets;
        map<set<Item>, int> itemSetMap;  // 用于跟踪项目集和状态的映射
    
        // 创建初始项目集
        set<Item> initialSet;
        initialSet.insert({ augmentedProductionIndex, 0 });
        initialSet = closure(initialSet);
        itemSets.push_back(initialSet);
        itemSetMap[initialSet] = 0;
        unprocessedSets.push(0);
    
        while (!unprocessedSets.empty()) {
            int currentIndex = unprocessedSets.front();
            unprocessedSets.pop();
            set<Item> currentSet = itemSets[currentIndex];
    
            set<string> allSymbols = terminals;
            allSymbols.insert(nonTerminals.begin(), nonTerminals.end());
            allSymbols.erase("ε");  // 移除ε符号
    
            for (const auto& symbol : allSymbols) {
                set<Item> newSet = goTo(currentSet, symbol);
    
                if (!newSet.empty()) {
                    // 检查新项目集是否已存在
                    auto it = itemSetMap.find(newSet);
                    int newIndex;
    
                    if (it == itemSetMap.end()) {
                        newIndex = static_cast<int>(itemSets.size());
                        itemSets.push_back(newSet);
                        itemSetMap[newSet] = newIndex;
                        unprocessedSets.push(newIndex);
                    } else {
                        newIndex = it->second;
                    }
    
                    // 不再在此处修改actionTable和gotoTable
                }
            }
        }
    }

    // 计算FIRST集
    void computeFirstSets() {
        // 初始化，所有终结符的FIRST集是自己
        for (const auto& term : terminals) {
            firstSet[term] = { term };
        }

        // 非终结符的FIRST集初始化
        for (const auto& nt : nonTerminals) {
            firstSet[nt] = {};
        }

        bool changed = true;
        while (changed) {
            changed = false;
            for (const auto& prod : productions) {
                const string& left = prod.left;
                const vector<string>& right = prod.right;

                // 如果是ε产生式
                if (prod.isEpsilon()) {
                    if (!firstSet[left].count("ε")) {
                        firstSet[left].insert("ε");
                        changed = true;
                    }
                    continue;
                }

                size_t prevSize = firstSet[left].size();
                bool allContainEpsilon = true;

                // 遍历右部符号
                for (const auto& sym : right) {
                    // 如果是终结符
                    if (isTerminal(sym) && sym != "ε") {
                        if (!firstSet[left].count(sym)) {
                            firstSet[left].insert(sym);
                            changed = true;
                        }
                        allContainEpsilon = false;
                        break;
                    }

                    // 非终结符
                    const set<string>& symFirst = firstSet[sym];
                    bool symContainsEpsilon = symFirst.count("ε") > 0;

                    // 将symFirst中非ε元素添加到left的FIRST集
                    for (const auto& s : symFirst) {
                        if (s != "ε" && !firstSet[left].count(s)) {
                            firstSet[left].insert(s);
                            changed = true;
                        }
                    }

                    // 如果当前符号没有ε，则停止
                    if (!symContainsEpsilon) {
                        allContainEpsilon = false;
                        break;
                    }
                }

                // 如果所有右部符号都包含ε，则添加ε
                if (allContainEpsilon && !firstSet[left].count("ε")) {
                    firstSet[left].insert("ε");
                    changed = true;
                }
            }
        }
    }

    // 计算FOLLOW集
    void computeFollowSets() {
        // 初始化
        for (const auto& nt : nonTerminals) {
            followSet[nt] = {};
        }
        followSet[startSymbol].insert("#");
    
        bool changed = true;
        while (changed) {
            changed = false;
            for (const auto& prod : productions) {
                const string& left = prod.left;
                const vector<string>& right = prod.right;
    
                for (size_t i = 0; i < right.size(); i++) {
                    const string& symbol = right[i];
                    if (!nonTerminals.count(symbol)) continue;
    
                    bool allCanBeEpsilon = true;
                    for (size_t j = i + 1; j < right.size(); j++) {
                        const string& next = right[j];
                        
                        // 添加FIRST(next) - {ε} 到FOLLOW(symbol)
                        if (terminals.count(next) && next != "ε") {
                            if (!followSet[symbol].count(next)) {
                                followSet[symbol].insert(next);
                                changed = true;
                            }
                            allCanBeEpsilon = false;
                            break;
                        }
    
                        // 非终结符
                        for (const auto& s : firstSet[next]) {
                            if (s != "ε" && !followSet[symbol].count(s)) {
                                followSet[symbol].insert(s);
                                changed = true;
                            }
                        }
    
                        // 如果FIRST(next)不包含ε，则停止
                        if (!firstSet[next].count("ε")) {
                            allCanBeEpsilon = false;
                            break;
                        }
                    }
    
                    // 特殊处理：产生式右部末尾的非终结符
                    if (i == right.size() - 1 || allCanBeEpsilon) {
                        for (const auto& s : followSet[left]) {
                            if (!followSet[symbol].count(s)) {
                                followSet[symbol].insert(s);
                                changed = true;
                            }
                        }
                    }
                }
            }
        }
    }

    // 构建LR(0)分析表（纯LR(0)，不使用FOLLOW集）
    virtual void buildLR0ParseTable() {
        // 清理之前的缓存数据
        actionTable.clear();
        gotoTable.clear();
        itemSets.clear();
        
        buildItemSets();
        
        // 1. 处理移进和GOTO动作
        set<string> allSymbols = terminals;
        allSymbols.insert(nonTerminals.begin(), nonTerminals.end());
        allSymbols.erase("ε");  // 移除ε符号
        
        for (size_t i = 0; i < itemSets.size(); i++) {
            const set<Item>& itemSet = itemSets[i];
            
            // 处理所有可能的符号
            for (const auto& symbol : allSymbols) {
                set<Item> newSet = goTo(itemSet, symbol);
                if (!newSet.empty()) {
                    // 查找新项目集对应的状态索引
                    auto it = find(itemSets.begin(), itemSets.end(), newSet);
                    if (it != itemSets.end()) {
                        int newIndex = static_cast<int>(distance(itemSets.begin(), it));
                        
                        if (isTerminal(symbol)) {
                            // LR(0)移进动作 - 直接添加，不检查冲突
                            string actionKey = "s" + to_string(newIndex);
                            actionTable[{static_cast<int>(i), symbol}] = actionKey;
                        } else {
                            // GOTO动作
                            gotoTable[{static_cast<int>(i), symbol}] = newIndex;
                        }
                    }
                }
            }
        }
        
        // 2. 处理规约和接受动作（LR(0)方式）
        for (size_t i = 0; i < itemSets.size(); i++) {
            const set<Item>& itemSet = itemSets[i];
            
            for (const auto& item : itemSet) {
                const Production& prod = productions[item.prodIndex];
                
                // 点在末尾（规约项目）
                if (static_cast<size_t>(item.dotPos) == prod.right.size()) {
                    // 接受项目：S' -> S·
                    if (item.prodIndex == augmentedProductionIndex) {
                        actionTable[{static_cast<int>(i), "#"}] = "acc";
                    }
                    // 规约项目 - LR(0)对所有终结符都添加规约动作
                    else {
                        string actionKey = "r" + to_string(item.prodIndex);
                        for (const auto& term : terminals) {
                            if (term == "ε") continue;
                            
                            // LR(0)直接添加规约动作，可能产生冲突
                            auto existingAction = actionTable.find({static_cast<int>(i), term});
                            if (existingAction != actionTable.end()) {
                                // 报告冲突但继续执行
                                cout << "LR(0) Conflict in state " << i << ", symbol " << term 
                                     << ": " << existingAction->second << " vs " << actionKey << endl;
                            }
                            actionTable[{static_cast<int>(i), term}] = actionKey;
                        }
                    }
                }
            }
        }
    }

    // 构建SLR(1)分析表
    virtual void buildSLR1ParseTable() {
        // 清理之前的缓存数据
        actionTable.clear();
        gotoTable.clear();
        firstSet.clear();
        followSet.clear();
        itemSets.clear();
        gotoTable.clear();
    
        computeFirstSets();
        computeFollowSets();
        buildItemSets();
    
        // 1. 处理移进和GOTO动作
        set<string> allSymbols = terminals;
        allSymbols.insert(nonTerminals.begin(), nonTerminals.end());
        allSymbols.erase("ε");  // 移除ε符号
    
        for (size_t i = 0; i < itemSets.size(); i++) {
            const set<Item>& itemSet = itemSets[i];
            
            // 处理所有可能的符号
            for (const auto& symbol : allSymbols) {
                set<Item> newSet = goTo(itemSet, symbol);
                if (!newSet.empty()) {
                    // 查找新项目集对应的状态索引
                    auto it = find(itemSets.begin(), itemSets.end(), newSet);
                    if (it != itemSets.end()) {
                        int newIndex = static_cast<int>(distance(itemSets.begin(), it));
                        
                        if (isTerminal(symbol)) {
                            // SLR(1)移进动作 - 只添加不冲突的移进
                            string actionKey = "s" + to_string(newIndex);
                            auto existingAction = actionTable.find({static_cast<int>(i), symbol});
                            
                            if (existingAction == actionTable.end()) {
                                actionTable[{static_cast<int>(i), symbol}] = actionKey;
                            }
                        } else {
                            // GOTO动作
                            gotoTable[{static_cast<int>(i), symbol}] = newIndex;
                        }
                    }
                }
            }
        }
    
        // 2. 处理规约和接受动作
        for (size_t i = 0; i < itemSets.size(); i++) {
            const set<Item>& itemSet = itemSets[i];
    
            for (const auto& item : itemSet) {
                const Production& prod = productions[item.prodIndex];
    
                // 点在末尾（规约项目）
                if (static_cast<size_t>(item.dotPos) == prod.right.size()) {
                    // 接受项目：S' -> S·
                    if (item.prodIndex == augmentedProductionIndex) {
                        actionTable[{static_cast<int>(i), "#"}] = "acc";
                    }
                    // 规约项目 - SLR(1)使用FOLLOW集
                    else {
                        // 对该非终结符的FOLLOW集中的每个终结符添加规约动作
                        const set<string>& follow = followSet[prod.left];
                        for (const auto& term : follow) {
                            if (term == "ε") continue;
                            
                            string actionKey = "r" + to_string(item.prodIndex);
                            auto existingAction = actionTable.find({static_cast<int>(i), term});
                            
                            // 解决移进-规约冲突：优先移进
                            if (existingAction != actionTable.end()) {
                                if (existingAction->second[0] == 's') {
                                    // 保留移进动作，跳过规约
                                    continue;
                                } else if (existingAction->second[0] == 'r') {
                                    throw runtime_error("Reduce-reduce conflict in state " + 
                                        to_string(i) + ", symbol " + term);
                                }
                            }
                            
                            actionTable[{static_cast<int>(i), term}] = actionKey;
                        }
                    }
                }
            }
        }
    }

    // 从输入加载文法
    void loadGrammar(const vector<string>& grammar) {
        nonTerminals.clear();
        terminals.clear();
        productions.clear();

        bool parsingProductions = false;  // 标记是否在解析产生式部分

        // 逐行处理文法定义
        for (const auto& line : grammar) {
            if (line.find("NonTerminals:") != string::npos) {
                // 解析非终结符
                auto parts = split(line.substr(line.find(":") + 1), ',');
                for (const auto& p : parts) {
                    if (!p.empty()) nonTerminals.insert(p);
                }
            }
            else if (line.find("Terminals:") != string::npos) {
                // 解析终结符
                auto parts = split(line.substr(line.find(":") + 1), ',');
                for (const auto& p : parts) {
                    if (!p.empty()) terminals.insert(p);
                }
                terminals.insert("#"); // 确保包含结束符
            }
            else if (line.find("StartSymbol:") != string::npos) {
                // 解析开始符号
                startSymbol = split(line.substr(line.find(":") + 1), ' ')[0];
            }
            else if (line.find("Productions:") != string::npos) {
                // 进入产生式解析部分
                parsingProductions = true;
            }
            else if (parsingProductions && !line.empty()) {
                // 解析产生式
                size_t arrowPos = line.find("->");
                if (arrowPos == string::npos) continue;

                // 获取左部
                string left = line.substr(0, arrowPos);
                // 修复lambda表达式中的问题
                left.erase(remove_if(left.begin(), left.end(), [](unsigned char c) {
                    return isspace(c);
                }), left.end());

                // 分割右部候选式
                string rightPart = line.substr(arrowPos + 2);
                vector<string> alternatives = split(rightPart, '|');

                // 为每个候选式创建产生式
                for (const auto& alt : alternatives) {
                    Production prod;
                    prod.left = left;
                    vector<string> symbols = split(alt, ' ');
                    for (const auto& s : symbols) {
                        if (s == "ε") {
                            prod.right = { "ε" };
                            break;
                        }
                        else if (!s.empty()) {
                            prod.right.push_back(s);
                        }
                    }
                    productions.push_back(prod);
                }
            }
        }

        // 文法扩展：添加S' -> S
        augmentedStartSymbol = startSymbol + "'";
        nonTerminals.insert(augmentedStartSymbol);

        Production augmentedProd;
        augmentedProd.left = augmentedStartSymbol;
        augmentedProd.right = { startSymbol };
        productions.insert(productions.begin(), augmentedProd);
        augmentedProductionIndex = 0; // 扩展产生式索引为0
    }

    // 语法分析过程
    bool parse(const string& input) {
        parseSteps.clear();
        vector<string> tokens = split(input, ' ');
        stack<int> stateStack;   // 状态栈
        stack<string> symbolStack; // 符号栈
        stateStack.push(0);       // 初始状态
        symbolStack.push("#");     // 栈底符号

        int step = 1;             // 步骤计数器
        size_t inputPtr = 0;      // 输入指针

        while (true) {
            // 获取当前状态和输入符号
            int currentState = stateStack.top();
            string currentToken = (inputPtr < tokens.size()) ? tokens[inputPtr] : "#";

            // 记录当前步骤信息
            ParseStep ps;
            ps.step = step;
            ps.stateStack = stackToString(stateStack);
            ps.symbolStack = stackToString(symbolStack);
            ps.currentInput = currentToken;
            ps.remainingInput = getRemainingInput(tokens, inputPtr);

            // 查找ACTION表
            auto actionIt = actionTable.find({ currentState, currentToken });
            if (actionIt == actionTable.end()) {
                ps.action = "Error: No ACTION entry";
                parseSteps.push_back(ps);
                parseResult = false;
                return false;
            }

            string action = actionIt->second;
            string actionDesc;

            // 处理动作
            if (action == "acc") {
                // 接受
                ps.action = "Accept";
                parseSteps.push_back(ps);
                parseResult = true;
                return true;
            }
            else if (action[0] == 's') {
                // 移进动作
                int nextState = stoi(action.substr(1));
                stateStack.push(nextState);
                symbolStack.push(currentToken);
                actionDesc = "Shift to state " + to_string(nextState);
                inputPtr++;
            }
            else if (action[0] == 'r') {
                // 规约动作
                int prodIndex = stoi(action.substr(1));
                Production prod = productions[prodIndex];

                // 弹出产生式右部
                for (size_t i = 0; i < prod.right.size(); i++) {
                    // 修复：正确处理ε产生式
                    if (prod.isEpsilon()) {
                        break; // 不需要弹出任何符号
                    }
                    stateStack.pop();
                    symbolStack.pop();
                }

                // 获取规约前的状态
                int prevState = stateStack.top();
                string leftSymbol = prod.left;

                // 查找GOTO表
                auto gotoIt = gotoTable.find({ prevState, leftSymbol });
                if (gotoIt == gotoTable.end()) {
                    ps.action = "Error: No GOTO entry";
                    parseSteps.push_back(ps);
                    parseResult = false;
                    return false;
                }

                // 压入新状态和符号
                int nextState = gotoIt->second;
                stateStack.push(nextState);
                symbolStack.push(leftSymbol);

                actionDesc = "Reduce: " + prod.left + " -> ";
                for (const auto& sym : prod.right) {
                    actionDesc += sym + " ";
                }
            }

            ps.action = actionDesc;
            parseSteps.push_back(ps);
            step++;
        }

        parseResult = false;
        return false;
    }

    // 将内部数据转换为Crow JSON格式
    crow::json::wvalue toJson() {
        crow::json::wvalue result;
        
        // 文法信息
        result["start_symbol"] = startSymbol;
        result["augmented_start_symbol"] = augmentedStartSymbol;
        
        // 非终结符
        vector<string> ntVec;
        for (const auto& nt : nonTerminals) {
            ntVec.push_back(nt);
        }
        result["non_terminals"] = ntVec;
        
        // 终结符
        vector<string> tVec;
        for (const auto& t : terminals) {
            tVec.push_back(t);
        }
        result["terminals"] = tVec;
        
        // 产生式
        vector<string> prodStrs;
        for (size_t i = 0; i < productions.size(); i++) {
            string prodStr = to_string(i) + ": " + productions[i].left + " -> ";
            for (const auto& sym : productions[i].right) {
                prodStr += sym + " ";
            }
            prodStrs.push_back(prodStr);
        }
        result["productions"] = prodStrs;
        
        // FIRST集
        crow::json::wvalue firstJson;
        for (const auto& [key, value] : firstSet) {
            if (key == augmentedStartSymbol) continue;
            vector<string> vals;
            for (const auto& v : value) {
                vals.push_back(v);
            }
            firstJson[key] = move(vals);
        }
        result["first_set"] = move(firstJson);
        
        // FOLLOW集
        crow::json::wvalue followJson;
        for (const auto& [key, value] : followSet) {
            if (key == augmentedStartSymbol) continue;
            vector<string> vals;
            for (const auto& v : value) {
                vals.push_back(v);
            }
            followJson[key] = move(vals);
        }
        result["follow_set"] = move(followJson);
        
        // 项目集族
        vector<crow::json::wvalue> itemSetJson;
        for (size_t i = 0; i < itemSets.size(); i++) {
            crow::json::wvalue setJson;
            setJson["state"] = static_cast<int>(i);
            
            vector<string> items;
            for (const auto& item : itemSets[i]) {
                const Production& prod = productions[item.prodIndex];
                string itemStr = prod.left + " -> ";
                
                for (size_t j = 0; j < prod.right.size(); j++) {
                    if (static_cast<int>(j) == item.dotPos) itemStr += ". ";
                    itemStr += prod.right[j] + " ";
                }
                
                if (item.dotPos == static_cast<int>(prod.right.size())) {
                    itemStr += ".";
                }
                items.push_back(itemStr);
            }
            setJson["items"] = move(items);
            itemSetJson.push_back(move(setJson));
        }
        result["item_sets"] = move(itemSetJson);
        
        // ACTION表
        crow::json::wvalue actionJson;
        for (const auto& [key, value] : actionTable) {
            string state = to_string(key.first);
            string symbol = key.second;
            actionJson[state][symbol] = value;
        }
        result["action_table"] = move(actionJson);
        
        // GOTO表
        crow::json::wvalue gotoJson;
        for (const auto& [key, value] : gotoTable) {
            string state = to_string(key.first);
            string symbol = key.second;
            gotoJson[state][symbol] = value;
        }
        result["goto_table"] = move(gotoJson);
        
        // 分析结果
        result["parse_result"] = parseResult;
        
        // 分析步骤
        vector<crow::json::wvalue> stepJson;
        for (const auto& step : parseSteps) {
            crow::json::wvalue s;
            s["step"] = step.step;
            s["state_stack"] = step.stateStack;
            s["symbol_stack"] = step.symbolStack;
            s["current_input"] = step.currentInput;
            s["remaining_input"] = step.remainingInput;
            s["action"] = step.action;
            stepJson.push_back(move(s));
        }
        result["parse_steps"] = move(stepJson);
        
        return result;
    }

private:
    // 辅助函数：将栈转为字符串（状态栈）
    string stackToString(stack<int> stk) {
        string result;
        stack<int> temp;
        while (!stk.empty()) {
            temp.push(stk.top());
            stk.pop();
        }
        while (!temp.empty()) {
            result += to_string(temp.top()) + " ";
            temp.pop();
        }
        return result;
    }

    // 辅助函数：将栈转为字符串（符号栈）
    string stackToString(stack<string> stk) {
        string result;
        stack<string> temp;
        while (!stk.empty()) {
            temp.push(stk.top());
            stk.pop();
        }
        while (!temp.empty()) {
            result += temp.top() + " ";
            temp.pop();
        }
        return result;
    }

    // 辅助函数：获取剩余输入字符串
    string getRemainingInput(const vector<string>& tokens, size_t pos) {
        string result;
        for (size_t i = pos; i < tokens.size(); ++i) {
            result += tokens[i];
            if (i < tokens.size() - 1) result += " ";
        }
        return result;
    }
};

// LR(0)语法分析器类
class LR0Parser : public ParserBase {
public:
    void buildParseTable() {
        buildLR0ParseTable();
    }
};

// SLR(1)语法分析器类  
class SLR1Parser : public ParserBase {
public:
    void buildParseTable() {
        buildSLR1ParseTable();
    }
};

// 解决CORS问题的中间件
struct CORSMiddleware {
    struct context {};
    
    void before_handle(crow::request& req, crow::response& res, context& ctx) {
        // 设置CORS头
        res.add_header("Access-Control-Allow-Origin", "*");
        res.add_header("Access-Control-Allow-Headers", "Content-Type");
        
        // 处理OPTIONS预检请求
        if (req.method == crow::HTTPMethod::Options) {
            res.code = crow::OK;
            res.end();
        }
    }
    
    void after_handle(crow::request& req, crow::response& res, context& ctx) {}
};

int main() {
    // 使用中间件创建应用
    crow::App<CORSMiddleware> app;

    LR0Parser lr0Parser;
    SLR1Parser slr1Parser;

    // API端点：加载文法
    CROW_ROUTE(app, "/api/load_grammar")
        .methods("POST"_method)
        ([&lr0Parser, &slr1Parser](const crow::request& req) {
            auto body = crow::json::load(req.body);
            if (!body) {
                return crow::response(400, "Invalid JSON");
            }

            vector<string> grammar;
            for (const auto& line : body["grammar"]) {
                grammar.push_back(line.s());
            }

            try {
                // 清理之前的缓存数据
                lr0Parser.clearCache();
                slr1Parser.clearCache();
                
                lr0Parser.loadGrammar(grammar);
                slr1Parser.loadGrammar(grammar);
                return crow::response(200, "Grammar loaded successfully");
            }
            catch (const exception& e) {
                return crow::response(500, string("Error loading grammar: ") + e.what());
            }
        });

    // API端点：构建LR(0)分析表
    CROW_ROUTE(app, "/api/build_lr0_table")
        .methods("GET"_method)
        ([&lr0Parser] {
            try {
                lr0Parser.buildParseTable();
                return crow::response(200, "LR(0) Parse table built successfully");
            }
            catch (const exception& e) {
                return crow::response(500, string("Error building LR(0) parse table: ") + e.what());
            }
        });

    // API端点：构建SLR(1)分析表
    CROW_ROUTE(app, "/api/build_table")
        .methods("GET"_method)
        ([&slr1Parser] {
            try {
                slr1Parser.buildParseTable();
                return crow::response(200, "SLR(1) Parse table built successfully");
            }
            catch (const exception& e) {
                return crow::response(500, string("Error building SLR(1) parse table: ") + e.what());
            }
        });

    // API端点：清理缓存
    CROW_ROUTE(app, "/api/clear_cache")
        .methods("POST"_method)
        ([&lr0Parser, &slr1Parser] {
            try {
                lr0Parser.clearCache();
                slr1Parser.clearCache();
                return crow::response(200, "Cache cleared successfully");
            }
            catch (const exception& e) {
                return crow::response(500, string("Error clearing cache: ") + e.what());
            }
        });

    // API端点：获取LR(0)分析表数据
    CROW_ROUTE(app, "/api/get_lr0_table_data")
        .methods("GET"_method)
        ([&lr0Parser] {
            try {
                auto json = lr0Parser.toJson();
                json["parser_type"] = "LR(0)";
                crow::response res(json);
                res.add_header("Content-Type", "application/json");
                return res;
            }
            catch (const exception& e) {
                return crow::response(500, string("Error getting LR(0) table data: ") + e.what());
            }
        });

    // API端点：获取SLR(1)分析表数据
    CROW_ROUTE(app, "/api/get_table_data")
        .methods("GET"_method)
        ([&slr1Parser] {
            try {
                auto json = slr1Parser.toJson();
                json["parser_type"] = "SLR(1)";
                crow::response res(json);
                res.add_header("Content-Type", "application/json");
                return res;
            }
            catch (const exception& e) {
                return crow::response(500, string("Error getting SLR(1) table data: ") + e.what());
            }
        });

    // API端点：使用LR(0)分析输入字符串
    CROW_ROUTE(app, "/api/parse_input_lr0")
        .methods("POST"_method)
        ([&lr0Parser](const crow::request& req) {
            auto body = crow::json::load(req.body);
            if (!body || !body.has("input")) {
                return crow::response(400, "Invalid JSON or missing 'input' field");
            }

            try {
                string input = body["input"].s();
                lr0Parser.parse(input);
                auto json = lr0Parser.toJson();
                json["parser_type"] = "LR(0)";
                crow::response res(json);
                res.add_header("Content-Type", "application/json");
                return res;
            }
            catch (const exception& e) {
                return crow::response(500, string("Error parsing input with LR(0): ") + e.what());
            }
        });

    // API端点：使用SLR(1)分析输入字符串
    CROW_ROUTE(app, "/api/parse_input")
        .methods("POST"_method)
        ([&slr1Parser](const crow::request& req) {
            auto body = crow::json::load(req.body);
            if (!body || !body.has("input")) {
                return crow::response(400, "Invalid JSON or missing 'input' field");
            }

            try {
                string input = body["input"].s();
                slr1Parser.parse(input);
                auto json = slr1Parser.toJson();
                json["parser_type"] = "SLR(1)";
                crow::response res(json);
                res.add_header("Content-Type", "application/json");
                return res;
            }
            catch (const exception& e) {
                return crow::response(500, string("Error parsing input with SLR(1): ") + e.what());
            }
        });

    // API端点：测试接口（为主页提供）
    CROW_ROUTE(app, "/api/hello")
        .methods("GET"_method)
        ([] {
            crow::json::wvalue result;
            result["message"] = "Hello from C++ backend!";
            result["status"] = "success";
            crow::response res(result);
            res.add_header("Content-Type", "application/json");
            return res;
        });

    // 启动服务器 (端口 8080)
    app.port(8080).multithreaded().run();

    return 0;
}