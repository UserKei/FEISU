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
    string left;           // 产生式左部
    vector<string> right;  // 产生式右部符号序列

    // 检查是否为ε产生式
    bool isEpsilon() const {
        return right.empty() || (right.size() == 1 && right[0] == "ε");
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

// LR(0)语法分析器类
class SLR1Parser {
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
        queue<int> unprocessedSets; // 待处理的项目集索引

        // 创建初始项目集：S' -> ·S
        set<Item> initialSet;
        initialSet.insert({ augmentedProductionIndex, 0 });
        itemSets.push_back(closure(initialSet));
        unprocessedSets.push(0);

        // 处理项目集直到队列为空
        while (!unprocessedSets.empty()) {
            int currentIndex = unprocessedSets.front();
            unprocessedSets.pop();
            set<Item> currentSet = itemSets[currentIndex];

            // 尝试所有可能的符号（终结符和非终结符）
            set<string> allSymbols = terminals;
            allSymbols.insert(nonTerminals.begin(), nonTerminals.end());

            for (const auto& symbol : allSymbols) {
                set<Item> newSet = goTo(currentSet, symbol);

                if (!newSet.empty()) {
                    // 检查新项目集是否已存在
                    auto it = find(itemSets.begin(), itemSets.end(), newSet);
                    int newIndex;

                    if (it == itemSets.end()) {
                        // 新项目集
                        newIndex = static_cast<int>(itemSets.size());
                        itemSets.push_back(newSet);
                        unprocessedSets.push(newIndex);
                    }
                    else {
                        // 已有项目集
                        newIndex = static_cast<int>(distance(itemSets.begin(), it));
                    }

                    // 记录转移关系（在构建分析表时使用）
                    if (isTerminal(symbol)) {
                        actionTable[{currentIndex, symbol}] = "s" + to_string(newIndex);
                    }
                    else {
                        gotoTable[{currentIndex, symbol}] = newIndex;
                    }
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
        // 开始符号的FOLLOW集包含#
        followSet[startSymbol].insert("#");

        bool changed = true;
        while (changed) {
            changed = false;
            for (const auto& prod : productions) {
                const string& left = prod.left;
                const vector<string>& right = prod.right;

                // 对于右部中的每个非终结符
                for (size_t i = 0; i < right.size(); i++) {
                    const string& symbol = right[i];
                    if (!nonTerminals.count(symbol)) continue; // 跳过终结符

                    bool epsilonTillEnd = true;
                    // 扫描后续符号
                    for (size_t j = i + 1; j < right.size(); j++) {
                        const string& next = right[j];

                        // 如果下一个是终结符
                        if (isTerminal(next) && next != "ε") {
                            // 添加到当前符号的FOLLOW集
                            if (!followSet[symbol].count(next)) {
                                followSet[symbol].insert(next);
                                changed = true;
                            }
                            epsilonTillEnd = false;
                            break;
                        }

                        // 非终结符
                        const set<string>& nextFirst = firstSet[next];
                        bool nextHasEpsilon = nextFirst.count("ε") > 0;

                        // 将nextFirst中非ε元素添加到symbol的FOLLOW集
                        for (const auto& s : nextFirst) {
                            if (s != "ε" && !followSet[symbol].count(s)) {
                                followSet[symbol].insert(s);
                                changed = true;
                            }
                        }

                        // 如果当前next没有ε，则停止
                        if (!nextHasEpsilon) {
                            epsilonTillEnd = false;
                            break;
                        }
                    }

                    // 如果从i+1到结尾都可以推导出ε，将左部的FOLLOW集添加过来
                    if (epsilonTillEnd) {
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

    // 构建SLR(1)分析表
    void buildParseTable() {
        actionTable.clear();
        gotoTable.clear();

        // 先计算FIRST和FOLLOW集
        computeFirstSets();
        computeFollowSets();

        // 构建项目集族（会填充部分ACTION和GOTO表）
        buildItemSets();

        // 处理规约和接受动作
        for (size_t i = 0; i < itemSets.size(); i++) {
            const set<Item>& itemSet = itemSets[i];

            for (const auto& item : itemSet) {
                const Production& prod = productions[item.prodIndex];

                // 点在末尾（规约项目）
                if (static_cast<size_t>(item.dotPos) == prod.right.size()) {
                    // 接受项目：S' -> S·
                    if (item.prodIndex == augmentedProductionIndex) {
                        // 只在遇到#时接受
                        if (actionTable.find({ static_cast<int>(i), "#" }) == actionTable.end()) {
                            actionTable[{static_cast<int>(i), "#"}] = "acc";
                        }
                        else {
                            throw runtime_error("Accept conflict in state " + to_string(i));
                        }
                    }
                    // 规约项目
                    else {
                        // 对该非终结符的FOLLOW集中的每个终结符添加规约动作
                        const set<string>& follow = followSet[prod.left];
                        for (const auto& term : follow) {
                            // 跳过ε
                            if (term == "ε") continue;

                            // 检查是否已有动作
                            auto it = actionTable.find({ static_cast<int>(i), term });
                            if (it != actionTable.end()) {
                                string currentAction = it->second;
                                if (currentAction[0] == 's') {
                                    throw runtime_error("Shift-reduce conflict in state " + to_string(i) + ", symbol " + term);
                                }
                                else if (currentAction[0] == 'r') {
                                    throw runtime_error("Reduce-reduce conflict in state " + to_string(i) + ", symbol " + term);
                                }
                                else if (currentAction == "acc") {
                                    throw runtime_error("Accept-reduce conflict in state " + to_string(i) + ", symbol " + term);
                                }
                            }
                            else {
                                actionTable[{static_cast<int>(i), term}] = "r" + to_string(item.prodIndex);
                            }
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
                    if (prod.right[i] == "ε") break; // ε产生式不需要弹出
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

    SLR1Parser parser;

    // API端点：加载文法
    CROW_ROUTE(app, "/api/load_grammar")
        .methods("POST"_method)
        ([&parser](const crow::request& req) {
            auto body = crow::json::load(req.body);
            if (!body) {
                return crow::response(400, "Invalid JSON");
            }

            vector<string> grammar;
            for (const auto& line : body["grammar"]) {
                grammar.push_back(line.s());
            }

            try {
                parser.loadGrammar(grammar);
                return crow::response(200, "Grammar loaded successfully");
            }
            catch (const exception& e) {
                return crow::response(500, string("Error loading grammar: ") + e.what());
            }
        });

    // API端点：构建分析表
    CROW_ROUTE(app, "/api/build_table")
        .methods("GET"_method)
        ([&parser] {
            try {
                parser.buildParseTable();
                return crow::response(200, "Parse table built successfully");
            }
            catch (const exception& e) {
                return crow::response(500, string("Error building parse table: ") + e.what());
            }
        });

    // API端点：获取分析表数据
    CROW_ROUTE(app, "/api/get_table_data")
        .methods("GET"_method)
        ([&parser] {
            try {
                auto json = parser.toJson();
                crow::response res(json);
                res.add_header("Content-Type", "application/json");
                return res;
            }
            catch (const exception& e) {
                return crow::response(500, string("Error getting table data: ") + e.what());
            }
        });

    // API端点：分析输入字符串
    CROW_ROUTE(app, "/api/parse_input")
        .methods("POST"_method)
        ([&parser](const crow::request& req) {
            auto body = crow::json::load(req.body);
            if (!body || !body.has("input")) {
                return crow::response(400, "Invalid JSON or missing 'input' field");
            }

            try {
                string input = body["input"].s();
                parser.parse(input);
                auto json = parser.toJson();
                crow::response res(json);
                res.add_header("Content-Type", "application/json");
                return res;
            }
            catch (const exception& e) {
                return crow::response(500, string("Error parsing input: ") + e.what());
            }
        });

    // 启动服务器 (端口 8080)
    app.port(8080).multithreaded().run();

    return 0;
}