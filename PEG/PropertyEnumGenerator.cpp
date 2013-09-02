/*
 * Copyright (c) 2012 - 2013, the Qtinuum project.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * <qtinuum.team@gmail.com>
 */

#include "PropertyEnumGenerator.h"

#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QStack>

namespace Qtinuum
{

PEG& peg = PEG::instance();

static QString QTN_NAMESPACE("Qtinuum::");

bool isPredefinedPropertyType(QString type)
{
    static QSet<QString> qtinuumPropertyNames;
    if (qtinuumPropertyNames.empty())
    {
        QSet<QString> names;
        names.insert("Bool");
        names.insert("Double");
        names.insert("Enum");
        names.insert("EnumFlags");
        names.insert("Float");
        names.insert("Int");
        names.insert("UInt");
        names.insert("Int64");
        names.insert("UInt64");
        names.insert("QRect");
        names.insert("QPoint");
        names.insert("QSize");
        names.insert("QString");
        names.insert("QColor");
        names.insert("QFont");

        foreach (QString name, names)
        {
            qtinuumPropertyNames.insert(name + "Callback");
        }

        qtinuumPropertyNames += names;
    }

    return qtinuumPropertyNames.contains(type);
}

static QString decorateStr(QString str, QString prefix, QString postfix = QString())
{
    if (str.isEmpty())
        return str;

    return prefix + str + postfix;
}

// special values that initialized in constructor
static Exceptions createSetExceptions()
{
    Exceptions exceptions;
    exceptions.insert("parent");
    return exceptions;
}
static Exceptions setExceptions = createSetExceptions();

static Signatures createSlotSignatures()
{
    Signatures _slots;
    _slots.insert("propertyWillChange",   "const " + QTN_NAMESPACE + "Property *changedProperty, "
                                        + "const " + QTN_NAMESPACE + "Property *firedProperty, "
                                        + QTN_NAMESPACE + "PropertyChangeReason reason, "
                                        + QTN_NAMESPACE + "PropertyValuePtr newValue");
    _slots.insert("propertyDidChange",    "const " + QTN_NAMESPACE + "Property *changedProperty, "
                                          + "const " + QTN_NAMESPACE + "Property *firedProperty, "
                                          + QTN_NAMESPACE + "PropertyChangeReason reason");
    _slots.insert("propertyValueAccept",  "const " + QTN_NAMESPACE + "Property *property, "
                                        + QTN_NAMESPACE + "PropertyValuePtr valueToAccept, "
                                        + "bool* accept");
    return _slots;
}
static Signatures slotsSignatures = createSlotSignatures();

static QString slotSignature(const QString &name)
{
    auto it = slotsSignatures.find(name);
    if (it == slotsSignatures.end())
        peg.fatalError("Cannot recognize slot " + name);

    return it.value();
}

static QString valueByName(const Assignments & assignments,
                           const QString & name,
                           const QString &defaultValue)
{
    auto it = assignments.find(name);
    if (it == assignments.end())
        return defaultValue;

    if (!it->member.isEmpty())
        return defaultValue;

    return it->value;
}

static QString capitalize(QString text)
{
    return text.left(1).toUpper() + text.mid(1);
}

static void assignmentSetCode(QString prefix,
                              Assignments::ConstIterator assignment,
                              TextStreamIndent& s)
{
    QString name;
    if (!prefix.isEmpty())
        name += prefix + ".";
    if (!assignment.value().member.isEmpty())
        name += assignment.value().member + ".";

    if (assignment.key() != "description")
    {
        s.newLine() << QString("%1set%2(%3);").arg(name
                                                 , capitalize(assignment.key())
                                                 , assignment.value().value);
    }
    else
    {
        QString var = name + assignment.key();
        var.replace(".", "_");
        s.newLine() << QString("static QString %1 = %2;").arg(var, assignment.value().value);
        s.newLine() << QString("%1set%2(%3);").arg(name
                                                 , capitalize(assignment.key())
                                                 , var);
    }
}

static void assignmentsSetCode(QString prefix,
                               const Assignments& assignments,
                               const Exceptions& exceptions,
                               TextStreamIndent& s)
{
    for (auto it = assignments.cbegin(); it != assignments.cend(); ++it)
    {
        if (!exceptions.contains(it.key()))
            assignmentSetCode(prefix, it, s);
    }
}

static QString slotName(const QString &name, const QString *member1, const QString *member2)
{
    QString slotName = "on_";
    if (member1 && !member1->isEmpty())
    {
        QString s = *member1;
        slotName += s.replace(QChar('.'), QChar('_')) + "_";
    }

    if (member2 && !member2->isEmpty())
    {
        QString s = *member2;
        slotName += s.replace(QChar('.'), QChar('_')) + "_";
    }

    return slotName + name;
}

void DelegateInfo::generateCode(TextStreamIndent &s) const
{
    s.newLine() << QString("QScopedPointer<%1PropertyDelegateInfo> info(new %1PropertyDelegateInfo());").arg(QTN_NAMESPACE);

    if (!name.isEmpty())
        s.newLine() << QString("info->name = \"%1\";").arg(name);

    for (auto it = attributes.begin(); it != attributes.end(); ++it)
    {
        s.newLine() << QString("info->attributes[\"%1\"] = %2;").arg(it.key(), it.value());
    }

    s.newLine() << "return info.take();";
}

IncludeCode::IncludeCode(QString name, bool isHeader)
    : name(name.trimmed()),
      isHeader(isHeader)
{}

void IncludeCode::generateHFile(TextStreamIndent &s) const
{
    if (!isHeader)
        return;
    generate(s);
}

void IncludeCode::generateCppFile(TextStreamIndent &s) const
{
    if (isHeader)
        return;
    generate(s);
}

SourceCode::SourceCode(const QString &code, bool code_h)
    : code(code),
      code_h(code_h)
{}

void SourceCode::generateHFile(TextStreamIndent &s) const
{
    if (!code_h)
        return;
    s.newLine() << code;
}

void SourceCode::generateCppFile(TextStreamIndent &s) const
{
    if (code_h)
        return;
    s.newLine() << code;
}

PropertySetCode::PropertySetCode()
    : _extern(false)
{}

void PropertySetCode::setName(QString _name)
{
    name = _name;
    selfType = "PropertySet" + name;
}

void PropertySetCode::setSuperType(const PropertySetCode* parent)
{
    if (parent->superType.isEmpty())
        superType = parent->selfType;
    else
        superType = parent->superType + "::" + parent->selfType;
}

void PropertySetCode::setConstructorCode(QString _name, QString _params_list,
                                         QString _initialization_list, QString _code)
{
    if (_name != name)
        peg.fatalError(QString("Cannot recognize as constructor '%1'.").arg(_name));

    if (constructor)
        peg.fatalError(QString("Constructor for '%1' already defined.").arg(_name));

    constructor.reset(new Constructor());
    constructor->params_list = _params_list;
    constructor->initialization_list = _initialization_list.trimmed();
    constructor->code = _code;
}

void PropertySetCode::setDestructorCode(QString _name, QString _code)
{
    if (_name != name)
        peg.fatalError(QString("Cannot recognize as destructor '%1'.").arg(_name));

    if (destructor)
        peg.fatalError(QString("Destructor for '%1' already defined.").arg(_name));

    destructor.reset(new Destructor());
    destructor->code = _code;
}

void PropertySetCode::generateHFile(TextStreamIndent &s) const
{
    generateSubPropertySetsDeclarations(s);
    s << endl;
    s.newLine() << QString("class %1: %2 %3").arg(selfType,
                                                  defaultParentTypeAccess(),
                                                  defaultParentType());
        generateRestParentTypes(s);
    s.newLine() << "{";
    s.addIndent();
        s.newLine() << "Q_OBJECT";
        s.newLine() << QString("//Q_DISABLE_COPY(%1)").arg(selfType);
        s << endl;
    s.newLine(-1) << "public:";
        s.newLine() << "// constructor declaration";
        s.newLine() << QString("explicit %1(%2QObject *parent = %3);")
                            .arg(selfType,
                                 decorateStr(constructorParamsList(), "", ", "),
                                 valueByName(assignments, "parent", "0"));
        s.newLine() << "// destructor declaration";
        s.newLine() << QString("virtual ~%1();").arg(selfType);
        generateChildrenDeclaration(s);
    s.newLine(-1) << "private:";
        s.newLine() << "void init();";
        s.newLine() << "void connectSlots();";
        s.newLine() << "void disconnectSlots();";
        s.newLine() << "void connectDelegates();";
        generateSlotsDeclaration(s);
    s.delIndent();
    generateHSourceCode(s);
    s.newLine() << "};";
}

void PropertySetCode::generateCppFile(TextStreamIndent &s) const
{
    generateSubPropertySetsImplementations(s);
    // constructor implementation
    s << endl;
    s.newLine() << QString("%1::%1(%2QObject *parent)")
                        .arg(selfType,
                             decorateStr(constructorParamsList(), "", ", "));
    s.addIndent();
    s.newLine() << ": " << defaultParentType() << "(parent)";
        generateChildrenInitialization(s);
        generateConstructorInitializationList(s);
    s.delIndent();
    s.newLine() << "{";
    s.addIndent();
        if (constructor)
            s.newLine() << constructor->code;
        s.newLine() << "init();";
        s.newLine() << "connectSlots();";
        s.newLine() << "connectDelegates();";
    s.delIndent();
    s.newLine() << "}";
    // destructor implementation
    s << endl;
    s.newLine() << selfType << "::~"<< selfType << "()";
    s.newLine() << "{";
    s.addIndent();
        if (destructor)
            s.newLine() << destructor->code;
        s.newLine() << "disconnectSlots();";
    s.delIndent();
    s.newLine() << "}";
    // initialization
    s << endl;
    s.newLine() << "void " << selfType << "::init()";
    s.newLine() << "{";
    s.addIndent();
        s.newLine() << QString("static QString %1_name = tr(\"%1\");").arg(name);
        s.newLine() << QString("setName(%1_name);").arg(name);
        assignmentsSetCode("", assignments, setExceptions, s);
        generateChildrenAssignment(s);
    s.delIndent();
    s.newLine() << "}";
    // slots connect/disconnect
    s << endl;
    s.newLine() << "void " << selfType << "::connectSlots()";
    s.newLine() << "{";
    s.addIndent();
        generateSlotsConnections(s, "connect");
    s.delIndent();
    s.newLine() << "}";
    s << endl;
    s.newLine() << "void " << selfType << "::disconnectSlots()";
    s.newLine() << "{";
    s.addIndent();
        generateSlotsConnections(s, "disconnect");
    s.delIndent();
    s.newLine() << "}";
    generateSlotsImplementation(s);
    // delegates connects
    s << endl;
    s.newLine() << "void " << selfType << "::connectDelegates()";
    s.newLine() << "{";
    s.addIndent();
        generateDelegatesConnection(s);
    s.delIndent();
    s.newLine() << "}";
    generateCppSourceCode(s);
}

QString PropertySetCode::defaultParentTypeAccess() const
{
    if (parentTypes.isEmpty())
        return "public";
    else
        return parentTypes.first().first;
}

QString PropertySetCode::defaultParentType() const
{
    if (parentTypes.isEmpty())
        return QTN_NAMESPACE + "Property";
    else
        return parentTypes.first().second;
}

QString PropertySetCode::constructorParamsList() const
{
    if (!constructor)
        return QString();

    return constructor->params_list;
}

void PropertySetCode::generateRestParentTypes(TextStreamIndent &s) const
{
    if (parentTypes.size() < 2)
        return;

    for (int i = 1; i < parentTypes.size(); ++i)
    {
        s.newLine(1) << ", " << parentTypes[i].first
                     << " " << parentTypes[i].second;
    }
}

void PropertySetCode::generateChildrenDeclaration(TextStreamIndent &s) const
{
    if (members.isEmpty())
        return;

    s.pushWrapperLines("// start children declarations", "// end children declarations");
    foreach (auto p, members)
    {
        s.newLine() << p->selfType << " &" << p->name << ";";
    }
    s.popWrapperLines();
}

void PropertySetCode::generateChildrenInitialization(TextStreamIndent &s) const
{
    if (members.isEmpty())
        return;

    for (auto it = members.cbegin(); it != members.cend(); ++it)
    {
        const PropertySetMemberCode &p = **it;
        s.newLine() << ", " << p.name << "(*new " << p.selfType
                    << "(this))";
    }
}

void PropertySetCode::generateChildrenAssignment(TextStreamIndent &s) const
{
    if (members.isEmpty())
        return;

    s.pushWrapperLines("// start children initialization", "// end children initialization");
    foreach (auto p, members)
    {
        s.newLine() << QString("static QString %1_name = tr(\"%1\");").arg(p->name);
        s.newLine() << QString("%1.setName(%1_name);").arg(p->name);
        assignmentsSetCode(p->name, p->assignments, setExceptions, s);
    }
    s.popWrapperLines();
}

void PropertySetCode::generateSubPropertySetsDeclarations(TextStreamIndent &s) const
{
    QVector<const PropertySetCode*> _subPropertySets = getSubpropertySets();
    if (_subPropertySets.isEmpty())
        return;

    for (auto it = _subPropertySets.begin(); it != _subPropertySets.end(); ++it)
    {
        (*it)->generateHFile(s);
    }
}

void PropertySetCode::generateSubPropertySetsImplementations(TextStreamIndent &s) const
{
    QVector<const PropertySetCode*> _subPropertySets = getSubpropertySets();
    if (_subPropertySets.isEmpty())
        return;

    for (auto it = _subPropertySets.begin(); it != _subPropertySets.end(); ++it)
    {
        (*it)->generateCppFile(s);
    }
}

void PropertySetCode::generateSlotsDeclaration(TextStreamIndent &s) const
{
    s.pushWrapperLines("// start slot declarations", "// end slot declarations");
    for (auto it = slots_code.begin(); it != slots_code.end(); ++it)
    {
        s.newLine() << QString("void %1(%2);").arg(slotName(it.key(), &it.value().member, 0)
                                                   , slotSignature(it.key()));
    }

    for (auto it = members.begin(); it != members.end(); ++it)
    {
        for (auto jt = (*it)->slots_code.begin(); jt != (*it)->slots_code.end(); ++jt)
        {
            s.newLine() << QString("void %1(%2);").arg(slotName(jt.key(), &(*it)->name, &jt.value().member)
                                                       , slotSignature(jt.key()));
        }
    }
    s.popWrapperLines();
}

void PropertySetCode::generateSlotsImplementation(TextStreamIndent &s) const
{
    for (auto it = slots_code.begin(); it != slots_code.end(); ++it)
    {
        s << endl;
        s.newLine() << QString("void %1::%2(%3)").arg(selfType
                                                      , slotName(it.key(), &it.value().member, 0)
                                                      , slotSignature(it.key()));
        s.newLine() << "{";
        s.addIndent();
            s.newLine() << it.value().value;
        s.delIndent();
        s.newLine() << "}";
    }

    for (auto it = members.begin(); it != members.end(); ++it)
    {
        for (auto jt = (*it)->slots_code.begin(); jt != (*it)->slots_code.end(); ++jt)
        {
            s << endl;
            s.newLine() << QString("void %1::%2(%3)").arg(selfType
                                                          , slotName(jt.key(), &(*it)->name, &jt.value().member)
                                                          , slotSignature(jt.key()));
            s.newLine() << "{";
            s.addIndent();
                s.newLine() << jt.value().value;
            s.delIndent();
            s.newLine() << "}";
        }
    }
}

void PropertySetCode::generateHSourceCode(TextStreamIndent &s) const
{
    for (auto it = sourceCodes.begin(); it != sourceCodes.end(); ++it)
    {
        if (!(*it)->code_h) continue;

        (*it)->generateHFile(s);
    }
}

void PropertySetCode::generateCppSourceCode(TextStreamIndent &s) const
{
    for (auto it = sourceCodes.begin(); it != sourceCodes.end(); ++it)
    {
        if ((*it)->code_h) continue;

        (*it)->generateCppFile(s);
    }
}

void PropertySetCode::generateDelegatesConnection(TextStreamIndent &s) const
{
    if (!delegateInfo.isNull())
    {
        s.newLine() << QString("setDelegateCallback([] () -> const %1PropertyDelegateInfo * {").arg(QTN_NAMESPACE);
        s.addIndent();
            delegateInfo->generateCode(s);
        s.delIndent();
        s.newLine() << "});";
    }

    foreach (auto p, members)
    {
        if (!p->delegateInfo.isNull())
        {
            s.newLine() << QString("%1.setDelegateCallback([] () -> const %2PropertyDelegateInfo * {").arg(p->name, QTN_NAMESPACE);
            s.addIndent();
                p->delegateInfo->generateCode(s);
            s.delIndent();
            s.newLine() << "});";
        }
    }
}

void PropertySetCode::generateConstructorInitializationList(TextStreamIndent &s) const
{
    if (!constructor || constructor->initialization_list.isEmpty())
        return;

    s.newLine() << ", " << constructor->initialization_list;
}

QString PropertySetCode::slotMember(const QString &name)
{
    if (!name.isEmpty())
        return "&" + name;
    else
        return "this";
}

QString PropertySetCode::slotMember(const QString &name, const QString &subMember)
{
    if (subMember.isEmpty())
        return "&" + name;
    else
        return "&" + name + "." + subMember;
}

void PropertySetCode::generateSlotsConnections(TextStreamIndent &s, const QString& name) const
{
    for (auto it = slots_code.begin(); it != slots_code.end(); ++it)
    {
        s.newLine() << QString("QObject::%1(%2, &%3Property::%4, this, &%5::%6);").arg(
                                  name
                                , slotMember(it.value().member)
                                , QTN_NAMESPACE
                                , it.key()
                                , selfType
                                , slotName(it.key(), &it.value().member, 0));
    }

    for (auto it = members.begin(); it != members.end(); ++it)
    {
        for (auto jt = (*it)->slots_code.begin(); jt != (*it)->slots_code.end(); ++jt)
        {
            s.newLine() << QString("QObject::%1(%2, &%3Property::%4, this, &%5::%6);").arg(
                                      name
                                    , slotMember((*it)->name, jt.value().member)
                                    , QTN_NAMESPACE
                                    , jt.key()
                                    , selfType
                                    , slotName(jt.key(), &(*it)->name, &jt.value().member));
        }
    }
}

QVector<const PropertySetCode*> PropertySetCode::getSubpropertySets() const
{
    QVector<const PropertySetCode*> _subPropertySets;
    QVectorIterator<QSharedPointer<PropertySetCode> > it(subPropertySets);
    while (it.hasNext())
    {
        const QSharedPointer<PropertySetCode> &ps = it.next();
        if (!ps->_extern)
            _subPropertySets.append(ps.data());
    }

    return _subPropertySets;
}

EnumCode::EnumCode(const QString &name)
    : name(name)
{}

void EnumCode::generateHFile(TextStreamIndent &s) const
{
    s.newLine();
    s.newLine() << QString("class %1").arg(name);
    s.newLine() << "{";
    s.newLine() << "public:";
        s.addIndent();
        if (!items.isEmpty())
        {
            s.newLine() << "enum Enum";
            s.newLine() << "{";
            s.addIndent();
                for (size_t i = 0, n = items.size(); i < n; ++i)
                {
                    const EnumItemCode &enumItem = items[i];
                    s.newLine() << QString("%1 = %2").arg(enumItem.name).arg(enumItem.id);
                    if ((i+1) != n)
                        s << ",";
                }
            s.delIndent();
            s.newLine() << "};";
        }
        s.newLine();
        s.newLine() << "static Qtinuum::EnumInfo &info();";
        s.newLine() << QString("static const unsigned int values_count = %1;").arg(items.size());
        s.delIndent();
    s.newLine() << "};";
}

void EnumCode::generateCppFile(TextStreamIndent &s) const
{
    s.newLine() << QString("static Qtinuum::EnumInfo &create_%1_info()").arg(name);
    s.newLine() << "{";
    s.addIndent();
        s.newLine() << "QVector<Qtinuum::EnumValueInfo> staticValues;";
        foreach(const EnumItemCode &enumItem, items)
        {
            QString states;
            if (!enumItem.states.isEmpty())
            {
                states += ", ";
                for (size_t i = 0, n = enumItem.states.size(); i < n; ++i)
                {
                    const QString &state = enumItem.states[i];
                    if (i != 0)
                        states += " | ";
                    states += "Qtinuum::EnumValueState";
                    states += capitalize(state);
                }
            }
            s.newLine() << QString("staticValues.append(Qtinuum::EnumValueInfo(%1::%2, %3%4));").arg(
                               name
                               , enumItem.name
                               , enumItem.text
                               , states);
        }
        s.newLine();
        s.newLine() << "static Qtinuum::EnumInfo enumInfo(staticValues);";
        s.newLine() << "return enumInfo;";
    s.delIndent();
    s.newLine() << "}";
    s.newLine();
    s.newLine() << QString("Qtinuum::EnumInfo &%1::info()").arg(name);
    s.newLine() << "{";
    s.addIndent();
        s.newLine() << QString("static Qtinuum::EnumInfo &enumInfo = create_%1_info();").arg(name);
        s.newLine() << "return enumInfo;";
    s.delIndent();
    s.newLine() << "}";
}

PegContext::PegContext()
    : currPropertySet(0),
      currProperty(0)
{}

PropertySetMemberCode *PegContext::current()
{
    if (currProperty)
        return currProperty;
    else
    {
        if (!currPropertySet)
            peg.fatalError("No current object.");

        return currPropertySet;
    }
}

QString PegContext::currentType() const
{
    if (currType.namespaceName.isEmpty())
        return currType.name;
    else
        return currType.namespaceName + "::" + currType.name;
}

QString PegContext::currentPropertyType() const
{
    if (currType.namespaceName.isEmpty())
    {
        if (isPredefinedPropertyType(currType.name))
            return QTN_NAMESPACE + "Property" + currType.name;
        else
            return currType.name;
    }
    else
    {
        return currType.namespaceName + "::" + currType.name;
    }
}

QString PegContext::currentPropertySetType() const
{
    if (currType.namespaceName.isEmpty())
        return "PropertySet" + currType.name;
    else
        return currType.namespaceName + "::" + "PropertySet" + currType.name;
}

void PegContext::startPropertySet()
{
    QSharedPointer<PropertySetCode> newPropertySet(new PropertySetCode());

    if (!propertySet)
    {
        propertySet = newPropertySet;
        subPropertySets.push(propertySet.data());
    }
    else
    {
        Q_ASSERT(!subPropertySets.isEmpty());
        PropertySetCode *currentPropertySet = subPropertySets.top();
        newPropertySet->setSuperType(currentPropertySet);
        currentPropertySet->subPropertySets.append(newPropertySet);
        subPropertySets.push(newPropertySet.data());
    }

    currPropertySet = subPropertySets.top();
}

void PegContext::commitPropertySet()
{
    subPropertySets.pop();

    if (subPropertySets.isEmpty())
    {
        peg.code.append(propertySet);
        propertySet.reset();
        currPropertySet = 0;
    }
    else
    {
        currPropertySet = subPropertySets.top();
    }
}

void PegContext::startProperty()
{
    QSharedPointer<PropertyCode> newProperty(new PropertyCode());
    currPropertySet->members.append(newProperty);
    currProperty = newProperty.data();
}

void PegContext::commitProperty()
{
    currProperty = 0;
}

bool PegContext::checkSlotName(const QString &name)
{
    return slotsSignatures.contains(name);
}
PegContext pegContext;

PEG &PEG::instance()
{
    static PEG peg;
    return peg;
}

PEG::PEG()
    : m_isValid(false),
      m_errStream(stderr)
{
}

bool PEG::start(QString hFileName, QString cppFileName)
{
    if (m_isValid)
    {
        printError(PEG::tr("PEG initialazed already"));
        return false;
    }

    QScopedPointer<QFile> hFile(new QFile(hFileName, this));
    if (!hFile->open(QFile::WriteOnly | QFile::Truncate))
    {
        printError(tr(QString("Cannot open '%1' file for writting").arg(hFileName).toLatin1()));
        return false;
    }

    QScopedPointer<QFile> cppFile(new QFile(cppFileName, this));
    if (!cppFile->open(QFile::WriteOnly | QFile::Truncate))
    {
        printError(tr(QString("Cannot open '%1' file for writting").arg(cppFileName).toLatin1()));
        return false;
    }

    QFileInfo fi(*hFile);
    m_hFileName = fi.completeBaseName() + "." + fi.suffix();
    m_hDefine = fi.baseName().toUpper() + "_" + fi.suffix().toUpper();

    m_hStream.setDevice(hFile.take());
    m_cppStream.setDevice(cppFile.take());

    m_isValid = true;
    return true;
}

void PEG::stop()
{
    generateOutputs();

    code.clear();
    m_isValid = false;
}

void PEG::printError(QString error)
{
    m_errStream << error << endl;
}

void PEG::fatalError(QString error)
{
    printError(error);
    exit(1);
}

void PEG::generateOutputs()
{
    generateHFile();
    generateCppFile();
}

void PEG::generateHFile()
{
    m_hStream << "#ifndef " << m_hDefine << endl;
    m_hStream << "#define " << m_hDefine << endl;

    for (auto it = code.begin(); it != code.end(); ++it)
    {
        (*it)->generateHFile(m_hStream);
    }

    m_hStream << endl << endl;
    m_hStream << "#endif // " << m_hDefine << endl;
}

void PEG::generateCppFile()
{
    m_cppStream << "#include \"" << m_hFileName << "\"" << endl;
    for (auto it = code.begin(); it != code.end(); ++it)
    {
        (*it)->generateCppFile(m_cppStream);
    }
    m_cppStream << endl;
}
/*
void PEG::addIncludeH(QString name)
{
    QSharedPointer<Code> code(new IncludeCode(name, true));
    m_code.push_back(code);
}

void PEG::addIncludeCpp(QString name)
{
    QSharedPointer<Code> code(new IncludeCode(name, false));
    m_code.push_back(code);
}

void PEG::startPropertySet(QString name, QString parentType)
{
    Q_ASSERT(!m_context->propertySet);

    m_context->propertySet.reset(new PropertySetCode(name, parentType));
}

void PEG::commitPropertySet()
{
    Q_ASSERT(m_context->propertySet);

    m_code.push_back(m_context->propertySet);
    m_context->propertySet.reset();
}

void PEG::startProperty(QString name, QString type)
{
    Q_ASSERT(!m_context->property);
    Q_ASSERT(m_context->propertySet);

    m_context->property.reset(new PropertyCode());
    m_context->property->name = name;
    m_context->property->type = type;
}

void PEG::commitProperty()
{
    Q_ASSERT(m_context->propertySet);
    Q_ASSERT(m_context->property);

    m_context->propertySet->properties.push_back(m_context->property);
    m_context->property.reset();
}

void PEG::addAssignment(QString name, QString value)
{
    if (name.isEmpty())
        fatalError("Assignment name is empty.");

    if (value.isEmpty())
        fatalError("Assignment value is empty");

    if (m_context->property)
        m_context->property->assignments.insert(name.trimmed(), value.trimmed());
    else if (m_context->propertySet)
        m_context->propertySet->assignments.insert(name.trimmed(), value.trimmed());
    else fatalError("Assignment is out of scope.");
}


*/
} // end namespace Qtinuum