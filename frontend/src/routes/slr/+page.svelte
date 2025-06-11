<script>
    import { onMount } from 'svelte';

    let activeSection = 'grammar-input';
    let loading = false;
    let error = null;
    
    // 输入数据和状态
    let grammarInput = "NonTerminals: E, T, F\nTerminals: +, *, (, ), id\nStartSymbol: E\nProductions:\nE -> E + T\nE -> T\nT -> T * F\nT -> F\nF -> ( E )\nF -> id";
    let parseInputString = "id + id * id";
    
    // 后端返回的数据
    let augmentedGrammar = "";
    let firstSets = {};
    let followSets = {};
    let lr0DfaStates = [];
    let lr0DfaTransitions = [];
    let lr0ActionTable = {};
    let lr0GotoTable = {};
    let slr1ActionTable = {};
    let slr1GotoTable = {};
    let parseSteps = [];
    let parseResult = "";
    
    // 导航函数
    function navigateTo(section) {
        activeSection = section;
        setTimeout(() => {
            const targetElement = document.getElementById(section);
            if (targetElement) {
                targetElement.scrollIntoView({ behavior: 'smooth' });
            }
        }, 0);
    }
    
    // 获取所有终结符
    function getTerminals(table) {
        const terms = new Set();
        for (const state in table) {
            for (const terminal in table[state]) {
                if (terminal !== '$') {
                    terms.add(terminal);
                }
            }
        }
        terms.add('$');
        return Array.from(terms).sort();
    }
    
    // 获取所有非终结符
    function getNonTerminals(table) {
        const nonTerms = new Set();
        for (const state in table) {
            for (const nonTerminal in table[state]) {
                nonTerms.add(nonTerminal);
            }
        }
        return Array.from(nonTerms).sort();
    }
    
    // 提交文法到后端
    async function handleGrammarSubmit() {
    loading = true;
    error = null;
    try {
        const grammarLines = grammarInput.split('\n').map(line => line.trim()).filter(line => line);
        
        const response = await fetch('/api/load_grammar', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify({
                grammar: grammarLines
            })
        });
        
        if (!response.ok) {
            // 处理错误响应（纯文本）
            const errorText = await response.text();
            throw new Error(errorText);
        }
        
        // 处理成功响应（纯文本）
        const resultText = await response.text();
        alert(resultText);
    } catch (err) {
        error = err.message;
        console.error('Grammar submit error:', err);
    } finally {
        loading = false;
    }
}
    
    // 构建分析表
    async function buildParseTable() {
    loading = true;
    error = null;
    try {
        // 构建分析表
        let response = await fetch('/api/build_table', { method: 'GET' });
        
        if (!response.ok) {
            const errorText = await response.text();
            throw new Error(errorText || 'Failed to build parse table');
        }
        
        // 返回纯文本消息
        const buildResult = await response.text();
        alert(buildResult);
        
        // 获取分析表数据（这次需要JSON）
        response = await fetch('/api/get_table_data', { method: 'GET' });
        
        if (!response.ok) {
            const errorText = await response.text();
            throw new Error(errorText || 'Failed to get table data');
        }
        
        // 正确解析JSON响应
        const data = await response.json();
        
        // 更新前端状态
        augmentedGrammar = generateAugmentedGrammar(data);
        firstSets = data.first_set;
        followSets = data.follow_set;
            
            // 格式化项目集族
            lr0DfaStates = formatItemSets(data.item_sets);
            
            // 格式化转换关系
            lr0DfaTransitions = formatTransitions(data);
            
            // 转换分析表格式
            lr0ActionTable = convertActionTable(data.action_table);
            lr0GotoTable = convertGotoTable(data.goto_table);
            
            // SLR(1)表格相同（在实际项目中可能需要特殊处理）
            slr1ActionTable = {...lr0ActionTable};
            slr1GotoTable = {...lr0GotoTable};
            
            // 导航到FIRST和FOLLOW集
            navigateTo('first-follow');
            
        } catch (err) {
            error = err.message;
            console.error('Table build error:', err);
        } finally {
            loading = false;
        }
    }
    
    // 语法分析
    async function handleParseString() {
    if (!parseInputString) return;
    loading = true;
    error = null;
    try {
        const response = await fetch('/api/parse_input', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify({
                input: parseInputString
            })
        });
        
        if (!response.ok) {
            // 处理错误响应（纯文本）
            const errorText = await response.text();
            throw new Error(errorText || 'Parsing failed');
        }
        
        // 正确解析JSON响应
        const data = await response.json();
        
        // 更新分析结果
        parseSteps = data.parse_steps || [];
        parseResult = data.parse_result ? "输入串符合文法！" : "输入串不符合文法！";
        
        // 导航到分析过程
        navigateTo('parse-process');
        
    } catch (err) {
        error = err.message;
        console.error('Parsing error:', err);
    } finally {
        loading = false;
    }
}
    
    // 生成拓广文法
    function generateAugmentedGrammar(data) {
        if (!data.productions || !data.augmented_start_symbol) return "";
        
        const augmented = `${data.augmented_start_symbol} -> ${data.start_symbol}`;
        const others = data.productions
            .slice(1) // 跳过扩展产生式
            .map(prodStr => {
                // 格式化每个产生式：去除索引
                const parts = prodStr.split(':');
                if (parts.length > 1) {
                    return parts[1];
                }
                return prodStr;
            });
            
        return [augmented, ...others].join('\n');
    }
    
    // 格式化项目集族
    function formatItemSets(itemSets) {
        if (!itemSets || !itemSets.length) return [];
        
        return itemSets.map((setObj, index) => {
            return {
                state: `I${index}`,
                items: setObj.items
            };
        });
    }
    
    // 格式化转换关系
    function formatTransitions(data) {
        const transitions = [];
        
        // 从ACTION表中提取shift动作（s开头的）
        for (const [state, symbols] of Object.entries(data.action_table)) {
            for (const [symbol, action] of Object.entries(symbols)) {
                if (typeof action === 'string' && action.startsWith('s')) {
                    transitions.push({
                        from: `I${state}`,
                        to: `I${action.substring(1)}`,
                        symbol
                    });
                }
            }
        }
        
        // 从GOTO表中提取转换
        for (const [state, symbols] of Object.entries(data.goto_table)) {
            for (const [symbol, nextState] of Object.entries(symbols)) {
                transitions.push({
                    from: `I${state}`,
                    to: `I${nextState}`,
                    symbol
                });
            }
        }
        
        return transitions;
    }
    
    // 转换Action表格式
    function convertActionTable(actionTable) {
        const result = {};
        
        for (const [state, symbols] of Object.entries(actionTable)) {
            const stateKey = `I${state}`;
            
            if (!result[stateKey]) {
                result[stateKey] = {};
            }
            
            for (const [symbol, value] of Object.entries(symbols)) {
                result[stateKey][symbol] = value;
            }
        }
        
        return result;
    }
    
    // 转换Goto表格式
    function convertGotoTable(gotoTable) {
        const result = {};
        
        for (const [state, symbols] of Object.entries(gotoTable)) {
            const stateKey = `I${state}`;
            
            if (!result[stateKey]) {
                result[stateKey] = {};
            }
            
            for (const [symbol, value] of Object.entries(symbols)) {
                result[stateKey][symbol] = value;
            }
        }
        
        return result;
    }
    
    // 页面加载时滚动到锚点
    onMount(() => {
        if (window.location.hash) {
            const hash = window.location.hash.substring(1);
            if (hash) {
                activeSection = hash;
                setTimeout(() => {
                    const targetElement = document.getElementById(hash);
                    if (targetElement) {
                        targetElement.scrollIntoView({ behavior: 'smooth' });
                    }
                }, 0);
            }
        }
    });
    
    // 计算FIRST和FOLLOW集的非终结符列表
    $: firstFollowNonTerminals = Object.keys(firstSets)
        .filter(key => key && key !== "#" && !key.endsWith("'"))
        .sort();
    // 计算LR(0)和SLR(1)表的终结符和非终结符列表
    $: lr0ActionTerminals = getTerminals(lr0ActionTable);
    $: lr0GotoNonTerminals = getNonTerminals(lr0GotoTable);
    $: lr0TableStates = Object.keys(lr0ActionTable).sort((a, b) => parseInt(a.substring(1)) - parseInt(b.substring(1)));

    $: slr1ActionTerminals = getTerminals(slr1ActionTable);
    $: slr1GotoNonTerminals = getNonTerminals(slr1GotoTable);
    $: slr1TableStates = Object.keys(slr1ActionTable).sort((a, b) => parseInt(a.substring(1)) - parseInt(b.substring(1)));
</script>

<style>
    /* 全局样式 */
    body {
        font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
        margin: 0;
        padding: 0;
        background-color: #f8f9fa;
        color: #333;
        line-height: 1.6;
    }

    h1, h2, h3 {
        color: #2c3e50;
        margin-top: 0;
    }

    .container {
        display: flex;
        min-height: 100vh;
    }

    .sidebar {
        width: 250px;
        background: linear-gradient(135deg, #2c3e50, #1a2530);
        color: white;
        padding: 20px;
        box-shadow: 2px 0 10px rgba(0,0,0,0.2);
        position: sticky;
        top: 0;
        height: 100vh;
        overflow-y: auto;
    }

    .sidebar h2 {
        text-align: center;
        margin-bottom: 30px;
        font-size: 1.4rem;
        padding-bottom: 15px;
        border-bottom: 1px solid rgba(255,255,255,0.2);
    }

    .sidebar ul {
        list-style: none;
        padding: 0;
    }

    .sidebar ul li {
        margin-bottom: 12px;
    }

    .sidebar ul li a {
        display: block;
        padding: 12px 15px;
        color: #ecf0f1;
        text-decoration: none;
        border-radius: 5px;
        transition: all 0.3s ease;
        font-size: 0.95rem;
        border-left: 3px solid transparent;
    }

    .sidebar ul li a:hover {
        background-color: rgba(52, 152, 219, 0.2);
        border-left: 3px solid #3498db;
    }

    .sidebar ul li a.active {
        background-color: #3498db;
        font-weight: bold;
        border-left: 3px solid #2980b9;
    }

    .main-content {
        flex-grow: 1;
        padding: 30px;
        background-color: #fff;
        overflow-y: auto;
    }

    .section {
        margin-bottom: 40px;
        padding: 25px;
        background-color: #fff;
        border-radius: 10px;
        box-shadow: 0 3px 15px rgba(0,0,0,0.08);
        transition: transform 0.3s ease, box-shadow 0.3s ease;
        border: 1px solid #e9ecef;
    }

    .section:hover {
        transform: translateY(-5px);
        box-shadow: 0 5px 20px rgba(0,0,0,0.1);
    }

    textarea {
        width: 100%;
        min-height: 180px;
        padding: 15px;
        border: 1px solid #ced4da;
        border-radius: 8px;
        font-family: monospace;
        font-size: 15px;
        resize: vertical;
        transition: border-color 0.3s;
    }

    textarea:focus {
        border-color: #3498db;
        outline: none;
        box-shadow: 0 0 0 3px rgba(52, 152, 219, 0.2);
    }

    button {
        padding: 12px 25px;
        background: linear-gradient(to right, #3498db, #2980b9);
        color: white;
        border: none;
        border-radius: 8px;
        cursor: pointer;
        font-size: 1rem;
        font-weight: 600;
        transition: all 0.3s ease;
        box-shadow: 0 4px 6px rgba(50, 50, 93, 0.11), 0 1px 3px rgba(0, 0, 0, 0.08);
    }

    button:hover {
        background: linear-gradient(to right, #2980b9, #3498db);
        transform: translateY(-2px);
        box-shadow: 0 7px 14px rgba(50, 50, 93, 0.1), 0 3px 6px rgba(0, 0, 0, 0.08);
    }

    button:disabled {
        background: #95a5a6;
        transform: none;
        box-shadow: none;
        cursor: not-allowed;
    }

    .btn-group {
        display: flex;
        gap: 15px;
        margin-top: 20px;
        flex-wrap: wrap;
    }

    input[type="text"] {
        width: 100%;
        padding: 12px 15px;
        margin-bottom: 15px;
        border: 1px solid #ced4da;
        border-radius: 8px;
        font-size: 16px;
        transition: border-color 0.3s;
    }

    input[type="text"]:focus {
        border-color: #3498db;
        outline: none;
        box-shadow: 0 0 0 3px rgba(52, 152, 219, 0.2);
    }

    table {
        width: 100%;
        border-collapse: collapse;
        margin-top: 20px;
        font-size: 0.95em;
        box-shadow: 0 1px 3px rgba(0,0,0,0.1);
    }

    table, th, td {
        border: 1px solid #dee2e6;
    }

    th {
        background-color: #f8f9fa;
        font-weight: 600;
        position: sticky;
        top: 0;
        z-index: 1;
        padding: 15px 12px;
    }

    td {
        padding: 12px;
    }

    tbody tr:nth-child(even) {
        background-color: #f8f9fa;
    }

    tbody tr:hover {
        background-color: #e9f7fe;
    }

    pre {
        background-color: #f8f9fa;
        padding: 20px;
        border-radius: 8px;
        overflow-x: auto;
        font-family: 'Courier New', monospace;
        font-size: 15px;
        line-height: 1.5;
        border: 1px solid #e9ecef;
    }

    /* DFA 样式 */
    .dfa-state {
        margin-bottom: 20px;
        padding: 20px;
        border: 1px solid #e0e0e0;
        border-radius: 8px;
        background-color: #ffffff;
        box-shadow: 0 2px 5px rgba(0,0,0,0.05);
    }
    
    .dfa-state h4 {
        margin-top: 0;
        margin-bottom: 15px;
        color: #2c3e50;
        font-size: 1.1em;
    }
    
    .dfa-state ul {
        list-style: none;
        padding: 0;
        margin: 0;
    }
    
    .dfa-state ul li {
        font-family: monospace;
        margin-bottom: 8px;
        padding: 5px;
        background-color: #f0f8ff;
        border-radius: 4px;
    }
    
    .dfa-transitions p {
        font-family: monospace;
        margin: 8px 0;
        padding: 8px 12px;
        background-color: #f8f8f8;
        border-radius: 6px;
        display: inline-block;
        border: 1px solid #e0e0e0;
    }

    /* 分析结果样式 */
    .parse-result {
        margin-top: 20px;
        padding: 15px;
        border-radius: 8px;
        text-align: center;
        font-weight: bold;
        font-size: 1.2rem;
    }
    
    .success {
        background-color: #d4edda;
        color: #155724;
        border: 1px solid #c3e6cb;
    }
    
    .failure {
        background-color: #f8d7da;
        color: #721c24;
        border: 1px solid #f5c6cb;
    }

    /* 加载状态样式 */
    .loading-overlay {
        position: fixed;
        top: 0;
        left: 0;
        width: 100%;
        height: 100%;
        background: rgba(255, 255, 255, 0.85);
        display: flex;
        justify-content: center;
        align-items: center;
        z-index: 1000;
        flex-direction: column;
    }
    
    .spinner {
        border: 5px solid #f3f3f3;
        border-top: 5px solid #3498db;
        border-radius: 50%;
        width: 50px;
        height: 50px;
        animation: spin 1s linear infinite;
        margin-bottom: 20px;
    }
    
    @keyframes spin {
        0% { transform: rotate(0deg); }
        100% { transform: rotate(360deg); }
    }
    
    .error-message {
        padding: 15px;
        margin: 20px 0;
        background-color: #f8d7da;
        color: #721c24;
        border-radius: 8px;
        font-weight: bold;
    }
    
    .info-box {
        background: #e3f2fd;
        padding: 15px;
        border-radius: 8px;
        margin-bottom: 20px;
        border-left: 4px solid #2196f3;
    }
    
    /* 响应式设计 */
    @media (max-width: 768px) {
        .container {
            flex-direction: column;
        }
        
        .sidebar {
            width: 100%;
            height: auto;
            position: relative;
        }
        
        .btn-group {
            flex-direction: column;
        }
    }
</style>

<div class="container">
    <!-- 加载状态覆盖层 -->
    {#if loading}
        <div class="loading-overlay">
            <div class="spinner"></div>
            <div>处理中，请稍候...</div>
        </div>
    {/if}
    
    <aside class="sidebar">
        <h2 style="color: aqua;">SLR(1) 语法分析器</h2>
        <ul>
            <li><a href="#grammar-input" class:active={activeSection === 'grammar-input'} on:click|preventDefault={() => navigateTo('grammar-input')}>文法输入</a></li>
            <li><a href="#first-follow" class:active={activeSection === 'first-follow'} on:click|preventDefault={() => navigateTo('first-follow')}>FIRST & FOLLOW 集</a></li>
            <li><a href="#augmented-grammar" class:active={activeSection === 'augmented-grammar'} on:click|preventDefault={() => navigateTo('augmented-grammar')}>拓广文法</a></li>
            <li><a href="#lr0-dfa" class:active={activeSection === 'lr0-dfa'} on:click|preventDefault={() => navigateTo('lr0-dfa')}>LR(0) DFA</a></li>
            <li><a href="#lr0-parse-table" class:active={activeSection === 'lr0-parse-table'} on:click|preventDefault={() => navigateTo('lr0-parse-table')}>LR(0) 分析表</a></li>
            <li><a href="#slr1-parse-table" class:active={activeSection === 'slr1-parse-table'} on:click|preventDefault={() => navigateTo('slr1-parse-table')}>SLR(1) 分析表</a></li>
            <li><a href="#parse-process" class:active={activeSection === 'parse-process'} on:click|preventDefault={() => navigateTo('parse-process')}>语法分析过程</a></li>
        </ul>
    </aside>

    <main class="main-content">
        {#if error}
            <div class="error-message">
                错误: {error}
            </div>
        {/if}
        
        <section id="grammar-input" class="section">
            <h2>文法输入</h2>
            <div class="info-box">
                <p>请输入文法规则，格式如下：</p>
                <ul>
                    <li>第一行：NonTerminals: [逗号分隔的非终结符列表]</li>
                    <li>第二行：Terminals: [逗号分隔的终结符列表]</li>
                    <li>第三行：StartSymbol: [开始符号]</li>
                    <li>第四行：Productions:</li>
                    <li>后续行：文法的产生式 (每个产生式一行)</li>
                </ul>
            </div>
            <textarea bind:value={grammarInput} placeholder="NonTerminals: E, T, F
Terminals: +, *, (, ), id
StartSymbol: E
Productions:
E -> E + T
E -> T
T -> T * F
T -> F
F -> ( E )
F -> id"></textarea>
            <div class="btn-group">
                <button on:click={handleGrammarSubmit}>加载文法</button>
                <button on:click={buildParseTable} disabled={!grammarInput}>构建分析表</button>
            </div>
        </section>

        <section id="first-follow" class="section">
            <h2>FIRST 集和 FOLLOW 集</h2>
            <div class="info-box">
                <p>文法加载后，点击"构建分析表"按钮生成FIRST和FOLLOW集。</p>
            </div>
            
            <h3>FIRST 集</h3>
            <table>
                <thead>
                    <tr>
                        <th>非终结符</th>
                        <th>FIRST(X)</th>
                    </tr>
                </thead>
                <tbody>
                    {#if Object.keys(firstSets).length > 0}
                        {#each firstFollowNonTerminals as nt}
                            <tr>
                                <td>{nt}</td>
                                <td>{`{ ${(firstSets[nt] || []).join(', ')} }`}</td>
                            </tr>
                        {/each}
                    {:else}
                        <tr>
                            <td colspan="2" style="text-align: center;">没有数据</td>
                        </tr>
                    {/if}
                </tbody>
            </table>

            <h3>FOLLOW 集</h3>
            <table>
                <thead>
                    <tr>
                        <th>非终结符</th>
                        <th>FOLLOW(X)</th>
                    </tr>
                </thead>
                <tbody>
                    {#if Object.keys(followSets).length > 0}
                        {#each firstFollowNonTerminals as nt}
                            <tr>
                                <td>{nt}</td>
                                <td>{`{ ${(followSets[nt] || []).join(', ')} }`}</td>
                            </tr>
                        {/each}
                    {:else}
                        <tr>
                            <td colspan="2" style="text-align: center;">没有数据</td>
                        </tr>
                    {/if}
                </tbody>
            </table>
        </section>

        <section id="augmented-grammar" class="section">
            <h2>拓广文法</h2>
            <div class="info-box">
                <p>为构建SLR(1)分析器而添加了一个新的开始符号产生式。</p>
            </div>
            <pre>{augmentedGrammar || '没有数据'}</pre>
        </section>

        <section id="lr0-dfa" class="section">
            <h2>LR(0) 确定有限自动机 (DFA)</h2>
            <div class="info-box">
                <p>项目集族（Item Sets）及其转移关系。</p>
            </div>
            
            <h3>LR(0) 状态 (Item Set)</h3>
            {#if lr0DfaStates.length > 0}
                {#each lr0DfaStates as state}
                    <div class="dfa-state">
                        <h4>{state.state}:</h4>
                        <ul>
                            {#each state.items as item}
                                <li>{item}</li>
                            {/each}
                        </ul>
                    </div>
                {/each}
            {:else}
                <p>没有可用数据</p>
            {/if}

            <h3>LR(0) 转换</h3>
            <div class="dfa-transitions">
                {#if lr0DfaTransitions.length > 0}
                    {#each lr0DfaTransitions as transition}
                        <p>{transition.from} --({transition.symbol})→ {transition.to}</p>
                    {/each}
                {:else}
                    <p>没有可用转换关系</p>
                {/if}
            </div>
        </section>

        <section id="lr0-parse-table" class="section">
            <h2>LR(0) 分析表 (Action & Goto)</h2>
            <div class="info-box">
                <p>注：SLR(1)使用此表解决冲突</p>
            </div>
            
            <h3>Action 表</h3>
            {#if Object.keys(lr0ActionTable).length > 0}
                <div style="overflow-x: auto;"> 
                    <table>
                        <thead>
                            <tr>
                                <th>状态</th>
                                {#each lr0ActionTerminals as terminal}
                                    <th>{terminal}</th>
                                {/each}
                            </tr>
                        </thead>
                        <tbody>
                            {#each lr0TableStates as state}
                                <tr>
                                    <td>{state}</td>
                                    {#each lr0ActionTerminals as terminal}
                                        <td>{lr0ActionTable[state]?.[terminal] || ''}</td>
                                    {/each}
                                </tr>
                            {/each}
                        </tbody>
                    </table>
                </div>
            {:else}
                <p>没有ACTION表数据</p>
            {/if}

            <h3>Goto 表</h3>
            {#if Object.keys(lr0GotoTable).length > 0}
                <div style="overflow-x: auto;"> 
                    <table>
                        <thead>
                            <tr>
                                <th>状态</th>
                                {#each lr0GotoNonTerminals as nonTerminal}
                                    <th>{nonTerminal}</th>
                                {/each}
                            </tr>
                        </thead>
                        <tbody>
                            {#each lr0TableStates as state}
                                <tr>
                                    <td>{state}</td>
                                    {#each lr0GotoNonTerminals as nonTerminal}
                                        <td>{lr0GotoTable[state]?.[nonTerminal] || ''}</td>
                                    {/each}
                                </tr>
                            {/each}
                        </tbody>
                    </table>
                </div>
            {:else}
                <p>没有GOTO表数据</p>
            {/if}
        </section>

        <section id="slr1-parse-table" class="section">
            <h2>SLR(1) 分析表</h2>
            <div class="info-box">
                <p>SLR(1)使用与LR(0)相同的项目集族，但利用FOLLOW集解决冲突</p>
            </div>
            
            <h3>Action 表</h3>
            {#if Object.keys(slr1ActionTable).length > 0}
                <div style="overflow-x: auto;"> 
                    <table>
                        <thead>
                            <tr>
                                <th>状态</th>
                                {#each slr1ActionTerminals as terminal}
                                    <th>{terminal}</th>
                                {/each}
                            </tr>
                        </thead>
                        <tbody>
                            {#each slr1TableStates as state}
                                <tr>
                                    <td>{state}</td>
                                    {#each slr1ActionTerminals as terminal}
                                        <td>{slr1ActionTable[state]?.[terminal] || ''}</td>
                                    {/each}
                                </tr>
                            {/each}
                        </tbody>
                    </table>
                </div>
            {:else}
                <p>没有ACTION表数据</p>
            {/if}

            <h3>Goto 表</h3>
            {#if Object.keys(slr1GotoTable).length > 0}
                <div style="overflow-x: auto;"> 
                    <table>
                        <thead>
                            <tr>
                                <th>状态</th>
                                {#each slr1GotoNonTerminals as nonTerminal}
                                    <th>{nonTerminal}</th>
                                {/each}
                            </tr>
                        </thead>
                        <tbody>
                            {#each slr1TableStates as state}
                                <tr>
                                    <td>{state}</td>
                                    {#each slr1GotoNonTerminals as nonTerminal}
                                        <td>{slr1GotoTable[state]?.[nonTerminal] || ''}</td>
                                    {/each}
                                </tr>
                            {/each}
                        </tbody>
                    </table>
                </div>
            {:else}
                <p>没有GOTO表数据</p>
            {/if}
        </section>

        <section id="parse-process" class="section">
            <h2>语法分析过程</h2>
            <div class="info-box">
                <p>输入要分析的字符串，然后点击"开始分析"按钮。</p>
            </div>
            
            <input type="text" bind:value={parseInputString} placeholder="例如: id + id * id">
            <button on:click={handleParseString} disabled={!parseInputString || Object.keys(lr0ActionTable).length === 0}>开始分析</button>

            {#if parseSteps.length > 0}
                <h3>分析步骤</h3>
                <div style="overflow-x: auto;"> 
                    <table>
                        <thead>
                            <tr>
                                <th>步骤</th>
                                <th>栈</th>
                                <th>输入串</th>
                                <th>动作</th>
                            </tr>
                        </thead>
                        <tbody>
                            {#each parseSteps as step}
                                <tr>
                                    <td>{step.step}</td>
                                    <td>{step.state_stack}</td>
                                    <td>{step.remaining_input}</td>
                                    <td>{step.action}</td>
                                </tr>
                            {/each}
                        </tbody>
                    </table>
                </div>
            {/if}

            {#if parseResult}
                <div class="parse-result {parseResult.includes('符合') ? 'success' : 'failure'}">
                    {parseResult}
                </div>
            {/if}
        </section>
    </main>
</div>