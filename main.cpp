#include <QtCore>
#include <QColor>

QStringList inputDirs;
QMap<QString, QString> appropNamedLibs, otherKnownLibs;
QMap<QString, QColor> coloring;
QRegularExpression fileIgnorePattern;
QStringList filePatterns;
QSet<QString> libraryIgnoreList;

enum Granularity{FINEST, CLASSES=FINEST, MODULES, TOPLEVELMODULES, COARSEST = TOPLEVELMODULES} granularity = Granularity::CLASSES;

QString nodeFromSourceFile(QString path)
{
    if (fileIgnorePattern.match(path).hasMatch())
        return "";

    QFileInfo fi(path);
    auto result = path;
    result.replace("." + fi.suffix(), "");
    if (granularity==CLASSES)
        return result;

    int i = result.lastIndexOf('/');
    if (i!=-1)
        result = result.mid(0, i);

    if (granularity==MODULES)
        return result;

    // (granularity==TOPLEVELMODULES)
    return result;
}

QString nodeFromInclude(QString srcNode, QString path, QString include)
{
    // This is rather greedy: Assume that all includes that don't contain a 
    // slash or a dot is part of the c++ standard (<list>, <iostream>, etc).
    static const QRegularExpression slashOrDot("\\.|/");
    if (include.lastIndexOf(slashOrDot)==-1)
        return "c++";

    int firstSlash = include.indexOf('/');
    if (firstSlash!=-1)
    {
        auto prefix = include.mid(0, firstSlash);
        if (appropNamedLibs.contains(prefix))
            return appropNamedLibs[prefix];
    }
    else
    {
        // Check if file exists in same directory as src file.
        if (QFileInfo(path + "/" + include).exists())
        {
            int dotIndex = include.lastIndexOf(slashOrDot);
            QString includeNode = include.mid(0, dotIndex);
            if (srcNode.endsWith(includeNode)) // src/X.cpp including X.h -> return "src/X"
                return srcNode;

            int lastSlash = srcNode.lastIndexOf('/');
            if (lastSlash==-1)
                return includeNode;
            return srcNode.mid(0, lastSlash) + "/" + includeNode;
        }
    }
    if (otherKnownLibs.contains(include))
        return otherKnownLibs[include];

    return nodeFromSourceFile(include);
}

QFileInfoList srcs(QString path)
{
    QFileInfoList result;
    QStringList dirs(path);
    while (!dirs.isEmpty())
    {
        QDir dir(dirs.takeFirst());
        for (auto entry: dir.entryInfoList(QDir::AllDirs|QDir::NoDotAndDotDot))
            dirs << entry.absoluteFilePath();

        for (auto entry: dir.entryInfoList(filePatterns, QDir::Files))
            result << entry;
    }
    return result;
}

typedef QString Node;
typedef QSet<QString> Nodes;
typedef QMap<Node, Nodes> Graph;

Nodes allNodes(const Graph& graph)
{
    QSet<QString> result;
    for (auto it = graph.begin(); it!=graph.end(); it++)
        result.insert(it.key()), result.unite(it.value());
    return result;
}

bool dependsOn(const Graph& graph, QString depender, QString dependee)
{
    if (depender==dependee)
        return true;

    Nodes visited;
    QList<Node> tmp = graph.value(depender).toList();
    while (!tmp.isEmpty())
    {
        auto node = tmp.takeFirst();
        if (node==dependee)
            return true;
        if (visited.contains(node))
            continue;
        visited.insert(node);
        tmp << graph.value(node).toList();
    }
    return false;
};

int main(int argc, char* argv[])
{
    if (argc>1)
    {
        QSettings settings(argv[1], QSettings::IniFormat);
        fileIgnorePattern = QRegularExpression{ settings.value("FileIgnorePattern").toString() };
        libraryIgnoreList = settings.value("LibraryIgnoreList").toStringList().toSet();
        filePatterns = settings.value("FilePatterns").toStringList();
        granularity = qBound(FINEST, static_cast<Granularity>(settings.value("Granularity").toInt()), COARSEST);

        settings.beginGroup("PrefixedHeaders");
        for (auto key: settings.allKeys())
            if (settings.value(key).toString().isEmpty())
                appropNamedLibs[key] = key;
            else
                appropNamedLibs[key] = settings.value(key).toString();
        settings.endGroup();

        settings.beginGroup("OtherKnownHeaders");
        for (auto key: settings.allKeys())
            for (auto header: settings.value(key).toStringList())
                otherKnownLibs.insert(header.trimmed(), key);
        settings.endGroup();

        settings.beginGroup("Coloring");
        for (auto key: settings.allKeys())
            coloring[key] = settings.value(key).value<QColor>();
        settings.endGroup();
    }
    for (int i= 2; i<argc; i++)
        inputDirs << argv[i];

    Graph graph;
    for (auto inputDir: inputDirs)
    {
        if (!inputDir.endsWith("/"))
            inputDir = inputDir+"/";
        auto srcs = ::srcs(inputDir);
        for (auto fileInfo: srcs)
        {
            auto path = fileInfo.absoluteFilePath();
            QFile file(path);
            if (!file.open(QFile::ReadOnly))
                exit(0);

            if (path.startsWith(inputDir))
                path = path.mid(inputDir.size());

            auto node = nodeFromSourceFile(path);
            if (node.isEmpty())
                continue;

            while (!file.atEnd())
            {
                auto line = file.readLine().trimmed();
                if (line.isEmpty())
                    continue;

                if (line[0]!='#')
                    continue;

                static const QRegularExpression includeStatement("# *include *[\"<](.*)[\">]");
                auto match = includeStatement.match(line);
                if (!match.hasMatch())
                    continue;

                const QString includePath = match.capturedTexts()[1];

                auto destinationNode = nodeFromInclude(node, fileInfo.absolutePath(), includePath);
                if (destinationNode.isEmpty())
                    continue;

                if (node==destinationNode)
                    continue;

                graph[node].insert(destinationNode);
            }
        }
    }

    auto quoted = [](const QString& s) {return "\"" + s + "\""; } ;

    // Start output:
    QTextStream stream(stdout);
    stream << "strict digraph{" << endl;

    auto nodes = allNodes(graph);
    auto libs = (QStringList() << appropNamedLibs.values() << otherKnownLibs.values()).toSet();

    // Node coloring:
    for (auto lib: libs)
        if (nodes.contains(lib) && !libraryIgnoreList.contains(lib))
            stream << quoted(lib) << QString("[style=filled, fillcolor = \"%1\"]").arg("Gray") << endl;

    for (auto node: nodes)
    {
        if (libraryIgnoreList.contains(node))
            continue;

        QColor color;
        auto blend = [](QColor& a, QColor b)
        {
            a = !a.isValid() ? b : QColor(qMin(255, a.red()+b.red()), qMin(255, a.green()+b.green()), qMin(255, a.blue()+b.blue()));
        };
        for (auto jt = coloring.begin(); jt!=coloring.end(); jt++)
            if (dependsOn(graph, node, jt.key()))
                blend(color, jt.value());

        if (color.isValid())
            stream << quoted(node) << QString("[style=filled, fillcolor = \"%1\"]").arg(color.name()) << endl;
    }

    // Edges:
    for (auto it = graph.begin(); it!=graph.end(); it++)
        for (auto dst: it.value())
            if(!libraryIgnoreList.contains(dst))
                stream << quoted(it.key()) << " -> " << quoted(dst) << endl;

    // End output:
    stream << "}" << endl;

    return 0;
}
