#include "core/editor/SpellChecker.h"
#include "core/config/ConfigManager.h"
#include "Logger.hpp"

#include <QFile>
#include <QTextStream>
#include <QDir>
#include <QStandardPaths>
#include <QRegularExpression>
#include <algorithm>

// ========== 单例实现 ==========

SpellChecker& SpellChecker::instance()
{
    static SpellChecker s_instance;
    return s_instance;
}

SpellChecker::SpellChecker()
{
    loadBuiltinDictionary();
    loadUserDictionary();
    // 从 ConfigManager 读取初始开关状态
    m_enabled = ConfigManager::instance().spellCheckEnabled();
}

// ========== 内置词典（基础英文常用词）==========

void SpellChecker::loadBuiltinDictionary()
{
    // 基础英文词典：覆盖常见单词，足以检测注释/字符串中的拼写错误
    // 代码标识符（CamelCase/含数字）会在 checkText 中被 isCodeIdentifier 跳过
    static const char* const kBuiltinWords[] = {
        // 冠词/代词/介词/连词
        "the","a","an","and","or","but","if","then","else","for","to","of","in","on","at","by",
        "with","from","into","onto","upon","over","under","above","below","between","through",
        "during","before","after","since","until","without","within","about","against","among",
        "around","beside","besides","between","beyond","despite","except","inside","outside",
        "toward","towards","via","per","than","that","this","these","those","what","which","who",
        "whom","whose","when","where","why","how","all","any","both","each","few","more","most",
        "other","some","such","no","nor","not","only","same","so","too","very","can","will","just",
        "should","now","here","there","your","their","its","our","my","his","her","they","them",
        "it","you","he","she","we","i","me","him","us","am","is","are","was","were","be","been",
        "being","have","has","had","having","do","does","did","doing","would","could","should",
        "may","might","must","shall","also","get","got","make","made","makes","making","go","goes",
        "went","gone","going",
        // 常见动词
        "is","are","was","were","be","been","being","have","has","had","do","does","did","will",
        "would","shall","should","can","could","may","might","must","get","got","go","goes","went",
        "make","made","take","took","taken","give","gave","given","come","came","see","saw","seen",
        "know","knew","known","think","thought","say","said","tell","told","find","found","feel",
        "felt","become","became","leave","left","put","mean","meant","keep","kept","let","begin",
        "began","seem","seemed","help","helped","show","showed","shown","hear","heard","play",
        "played","run","ran","move","moved","live","lived","believe","believed","hold","held",
        "bring","brought","happen","happened","write","wrote","written","sit","sat","stand",
        "stood","lose","lost","pay","paid","meet","met","include","included","continue","continued",
        "set","learn","learned","learnt","change","changed","lead","led","understand","understood",
        "watch","watched","follow","followed","stop","stopped","create","created","speak","spoke",
        "spoken","read","allow","allowed","add","added","spend","spent","grow","grew","grown",
        "open","opened","walk","walked","win","won","offer","offered","remember","remembered",
        "love","loved","consider","considered","appear","appeared","buy","bought","wait","waited",
        "serve","served","die","died","send","sent","expect","expected","build","built","stay",
        "stayed","fall","fell","fallen","cut","reach","reached","kill","killed","remain","remained",
        // 常见名词
        "time","year","day","week","month","people","man","woman","child","boy","girl","world",
        "life","hand","part","place","case","week","company","system","program","group","number",
        "point","home","room","mother","father","parent","child","family","friend","woman","man",
        "girl","boy","son","daughter","brother","sister","question","way","name","music","water",
        "art","money","story","fact","month","lot","right","study","book","eye","job","word",
        "business","issue","side","kind","head","house","service","friend","father","power",
        "hour","game","line","end","member","law","car","city","community","name","president",
        "team","minute","idea","kid","body","information","back","parent","face","others","level",
        "office","door","health","person","art","war","history","party","result","change","reason",
        "research","girl","guy","moment","air","teacher","force","education","foot","boy","age",
        "policy","music","market","sense","nation","plan","college","interest","death","experience",
        "effect","use","class","control","care","field","development","role","effort","rate","heart",
        "drug","show","leader","light","voice","wife","police","mind","price","report","decision",
        "hope","research","view","relationship","town","road","arm","sound","paper","activity",
        "course","century","evidence","page","picture","dollars","truth","society","camera","structure",
        // 常见形容词
        "good","new","first","last","long","great","little","own","other","old","right","big",
        "high","different","small","large","next","early","young","important","few","public","bad",
        "same","able","best","real","sure","whole","free","full","low","short","easy","open","hard",
        "special","difficult","available","likely","recent","certain","wrong","happy","beautiful",
        "simple","common","poor","natural","significant","similar","hot","dead","central","happy",
        "serious","ready","green","nice","huge","popular","traditional","cultural","bright","clean",
        "fair","wide","smart","warm","safe","rich","weak","fresh","deep","strong","true","false",
        "empty","full","quiet","still","calm","cold","dark","dry","flat","gentle","great","heavy",
        "light","narrow","noisy","rough","sharp","smooth","soft","solid","steady","steep","straight",
        "sudden","sweet","thick","thin","tight","tiny","wet","wild","wonderful","yellow","angry",
        "blue","busy","careful","crazy","dumb","eager","fancy","fine","funny","gentle","glad",
        "grateful","great","guilty","handy","helpful","honest","kind","lazy","lonely","lucky",
        "mean","modest","nervous","odd","patient","perfect","polite","proud","rude","silly","smart",
        "strict","stubborn","stupid","sweet","tactful","tense","terrible","thoughtful","timid",
        "useful","vain","vivid","weak","weird","wise","witty","wrong",
        // 常见副词
        "very","really","also","just","too","well","only","even","still","yet","never","always",
        "often","sometimes","usually","rarely","seldom","soon","now","then","here","there","today",
        "tomorrow","yesterday","tonight","early","late","later","quickly","slowly","easily",
        "hardly","almost","enough","quite","rather","pretty","somehow","somewhere","anywhere",
        "everywhere","nowhere","instead","indeed","perhaps","maybe","certainly","definitely",
        "probably","possibly","luckily","unfortunately","suddenly","gradually","finally","eventually",
        // 编程/技术常用词
        "file","code","data","value","type","name","list","item","error","message","string",
        "number","array","object","class","method","function","return","param","parameter","argument",
        "variable","const","constant","define","definition","declare","declaration","import",
        "export","module","library","version","feature","option","config","configuration","setting",
        "default","custom","local","global","static","public","private","protected","virtual",
        "abstract","interface","struct","enum","property","event","handler","callback","delegate",
        "thread","process","memory","buffer","stream","socket","connection","request","response",
        "server","client","user","login","logout","session","token","password","username","account",
        "permission","access","read","write","update","delete","remove","create","insert","select",
        "query","table","column","row","record","field","index","key","source","target","source",
        "destination","output","input","print","print","log","debug","info","warning","warn",
        "fail","failed","success","successful","complete","completed","start","started","begin",
        "stop","stopped","pause","resume","run","running","exit","close","opened","closed","save",
        "saved","load","loaded","parse","parsed","format","formatted","valid","invalid","true",
        "false","null","none","empty","null","true","false","test","testing","tested","build",
        "built","compile","compiled","link","linked","deploy","deployed","install","installed",
        "uninstall","uninstalled","setup","setting","config","enable","enabled","disable","disabled",
        "check","checked","verify","verified","validate","validated","support","supported","require",
        "required","optional","available","unavailable","visible","hidden","shown","displayed",
        "present","absent","exist","exists","existing","previous","next","current","initial","final"
    };

    for (const char* w : kBuiltinWords) {
        m_dictionary.insert(QString::fromLatin1(w));
    }
}

// ========== 用户词典持久化 ==========

QString SpellChecker::userDictionaryPath() const
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    return dir + QStringLiteral("/user_dictionary.txt");
}

void SpellChecker::loadUserDictionary()
{
    QFile f(userDictionaryPath());
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;

    QTextStream in(&f);
    while (!in.atEnd()) {
        QString word = in.readLine().trimmed().toLower();
        if (word.isEmpty()) continue;
        m_userWords.insert(word);
        m_dictionary.insert(word);
    }
    LOG_DEBUG("[SpellChecker] 用户词典加载: " << m_userWords.size() << " 个单词");
}

void SpellChecker::saveUserDictionary()
{
    QFile f(userDictionaryPath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        LOG_DEBUG("[SpellChecker] 用户词典保存失败: " << f.fileName().toStdString());
        return;
    }
    QTextStream out(&f);
    // 排序输出，便于人工查看/编辑
    QStringList words = m_userWords.values();
    words.sort();
    for (const QString& w : words) {
        out << w << '\n';
    }
}

void SpellChecker::reloadUserDictionary()
{
    // 重建词典：清空后重新加载内置 + 用户词典
    m_userWords.clear();
    m_dictionary.clear();
    loadBuiltinDictionary();
    loadUserDictionary();
}

// ========== 公共 API ==========

void SpellChecker::setEnabled(bool enabled)
{
    m_enabled = enabled;
}

bool SpellChecker::isCorrect(const QString& word) const
{
    if (word.isEmpty()) return true;
    return m_dictionary.contains(word.toLower());
}

void SpellChecker::addToDictionary(const QString& word)
{
    QString lower = word.trimmed().toLower();
    if (lower.isEmpty()) return;
    m_userWords.insert(lower);
    m_dictionary.insert(lower);
    saveUserDictionary();
    LOG_DEBUG("[SpellChecker] 添加到用户词典: " << lower.toStdString());
}

// ========== 代码标识符过滤 ==========

bool SpellChecker::isCodeIdentifier(const QString& word)
{
    // 含数字或下划线 → 视为标识符，跳过
    for (const QChar& ch : word) {
        if (ch.isDigit() || ch == QLatin1Char('_')) {
            return true;
        }
    }
    // 混合大小写（CamelCase / PascalCase）→ 视为标识符，跳过
    // 例外：首字母大写其余小写（如 "The"）或全大写（如 "URL"）视为普通单词
    bool hasNonFirstUpper = false;  // 非首字母位置是否出现大写
    for (int i = 0; i < word.size(); ++i) {
        QChar ch = word[i];
        if (ch.isUpper() && i > 0) {
            hasNonFirstUpper = true;
            break;
        }
    }
    // 非首字母大写（如 "myVar" / "MyClass"）属 CamelCase/PascalCase，跳过
    if (hasNonFirstUpper) return true;
    // 全大写单词（如 "URL"）或仅首字母大写（如 "The"）按普通单词处理
    return false;
}

// ========== 文本扫描 ==========

QList<SpellMisspelledRange> SpellChecker::checkText(const QString& text) const
{
    QList<SpellMisspelledRange> result;
    if (!m_enabled || text.isEmpty()) return result;

    // 匹配纯字母单词（长度 >= 2）
    static const QRegularExpression wordRe(QStringLiteral("[A-Za-z]{2,}"));
    auto it = wordRe.globalMatch(text);
    while (it.hasNext()) {
        QRegularExpressionMatch m = it.next();
        QString word = m.captured();
        int start = m.capturedStart();
        int length = m.capturedLength();

        // 跳过代码标识符（CamelCase / 含数字等）
        if (isCodeIdentifier(word)) continue;

        // 词典查询（小写）
        if (!m_dictionary.contains(word.toLower())) {
            SpellMisspelledRange range;
            range.start = start;
            range.length = length;
            range.word = word;
            result.append(range);
        }
    }
    return result;
}

// ========== Levenshtein 编辑距离（建议算法）==========

int SpellChecker::levenshtein(const QString& a, const QString& b)
{
    const int m = a.size();
    const int n = b.size();
    if (m == 0) return n;
    if (n == 0) return m;

    // 滚动数组优化空间到 O(n)
    QList<int> prev(n + 1);
    QList<int> curr(n + 1);
    for (int j = 0; j <= n; ++j) prev[j] = j;

    for (int i = 1; i <= m; ++i) {
        curr[0] = i;
        for (int j = 1; j <= n; ++j) {
            int cost = (a[i - 1].toLower() == b[j - 1].toLower()) ? 0 : 1;
            int del = prev[j] + 1;
            int ins = curr[j - 1] + 1;
            int sub = prev[j - 1] + cost;
            curr[j] = std::min({del, ins, sub});
        }
        std::swap(prev, curr);
    }
    return prev[n];
}

QStringList SpellChecker::suggestions(const QString& word, int maxCount) const
{
    QStringList result;
    if (word.isEmpty()) return result;

    QString lower = word.toLower();
    // 候选：编辑距离 1 优先，不足再取距离 2，最后按字母序取前 maxCount
    struct Cand {
        QString word;
        int dist;
    };
    QList<Cand> dist1;
    QList<Cand> dist2;

    for (const QString& dictWord : m_dictionary) {
        // 长度差异过大直接跳过（编辑距离下界 = 长度差）
        int lenDiff = qAbs(dictWord.size() - lower.size());
        if (lenDiff > 2) continue;

        int d = levenshtein(lower, dictWord);
        if (d == 1) {
            dist1.append({dictWord, d});
        } else if (d == 2) {
            dist2.append({dictWord, d});
        }
    }

    // 距离 1 的候选按字母序取，不足再补距离 2
    std::sort(dist1.begin(), dist1.end(),
              [](const Cand& a, const Cand& b) { return a.word < b.word; });
    std::sort(dist2.begin(), dist2.end(),
              [](const Cand& a, const Cand& b) { return a.word < b.word; });

    for (const auto& c : dist1) {
        result.append(c.word);
        if (result.size() >= maxCount) return result;
    }
    for (const auto& c : dist2) {
        result.append(c.word);
        if (result.size() >= maxCount) return result;
    }
    return result;
}
