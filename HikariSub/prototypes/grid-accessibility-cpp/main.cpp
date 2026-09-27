// Throwaway native feasibility probe; no production or OS accessibility claim.
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickPaintedItem>
#include <QQuickWindow>
#include <QPainter>
#include <QAccessible>
#include <QAccessibleObject>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPointer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QDir>
#include <QCryptographicHash>
#include <QTimer>
#include <QRegularExpression>
#include <QSysInfo>
#include <QFontDatabase>
#include <algorithm>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

constexpr int RowTotal = 50000, Columns = 4, RowHeight = 23, HeaderHeight = 28;
static QJsonArray interfaceEvents;
static QJsonArray applicationFonts;
static int cellsCreated = 0, cellsDestroyed = 0;
static void observeEvent(QAccessibleEvent *event) {
    // In-process replacement handler. This deliberately is not an OS event observer.
    if (interfaceEvents.size() < 300)
        interfaceEvents.append(QJsonObject{{"type", int(event->type())}, {"accessible_id", double(event->uniqueId())}});
}
static QString rawText(int id) {
    return id % 3 == 0 ? QString::fromUtf8("{\\i1}مرحبا · سطر %1{\\i0}").arg(id)
                      : QString::fromUtf8("{\\b1}橋の灯り · Harbor %1{\\b0}").arg(id);
}
static QString header(int column) {
    static const QStringList names = {"Line ID", "Start (ms)", "End (ms)", "Subtitle text"};
    return names.value(column);
}
class GridAccessible;
class PaintedGrid : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(QString stateText READ stateText NOTIFY changed)
public:
    QVector<int> order, positions;
    QSet<int> selected{1};
    int current = 1, anchor = 1, currentColumn = 0, top = 0, maxPaintRows = 0;
    bool reversed = false, tagsHidden = false;
    QString filter = "all";
    PaintedGrid(QQuickItem *parent = nullptr) : QQuickPaintedItem(parent) {
        setAcceptedMouseButtons(Qt::LeftButton);
        setFlag(ItemIsFocusScope, true); setActiveFocusOnTab(true); rebuild(false);
    }
    int index(int id) const { return id > 0 && id <= RowTotal ? positions[id] : -1; }
    int pageRows() const { return qMax(1, int((height()-HeaderHeight)/RowHeight)); }
    QString value(int id, int col) const {
        if (col == 0) return QString::number(id);
        if (col == 1) return QString::number((id-1)*1500);
        if (col == 2) return QString::number((id-1)*1500+1200);
        auto text = rawText(id);
        if (tagsHidden) text.remove(QRegularExpression("\\{[^}]*\\}"));
        return text;
    }
    int hiddenSelected() const { int n=0; for (int id:selected) if(index(id)<0) ++n; return n; }
    QString stateText() const {
        return QString("Displayed %1 / %2 · Current Line %3 (display row %4) · Selected %5 (%6 hidden) · Anchor %7 · %8 · %9 · Top row %10")
          .arg(order.size()).arg(RowTotal).arg(current).arg(index(current)+1).arg(selected.size()).arg(hiddenSelected())
          .arg(anchor).arg(reversed?"reverse":"normal").arg(tagsHidden?"tags hidden":"raw tags").arg(top+1);
    }
    void notify(QAccessible::Event type) {
        QAccessibleEvent event(this,type); QAccessible::updateAccessibility(&event);
    }
    void refresh(bool selection=true) {
        update(); emit changed(); notify(QAccessible::NameChanged);
        if(selection) notify(QAccessible::SelectionWithin);
    }
    void rebuild(bool announce=true) {
        order.clear(); positions.fill(-1,RowTotal+1);
        if(filter!="empty") for(int id=1;id<=RowTotal;++id) if(filter!="rtl" || id%3==0) order.append(id);
        if(reversed) std::reverse(order.begin(),order.end());
        for(int i=0;i<order.size();++i) positions[order[i]]=i;
        top=qBound(0,top,qMax(0,int(order.size())-pageRows()));
        if(announce) {
            QAccessibleTableModelChangeEvent event(this,QAccessibleTableModelChangeEvent::ModelReset);
            QAccessible::updateAccessibility(&event); notify(QAccessible::ObjectReorder); refresh();
        }
    }
    Q_INVOKABLE void reverseRows() { reversed=!reversed; rebuild(); }
    Q_INVOKABLE void filterRows(QString mode) { filter=mode; top=0; rebuild(); }
    Q_INVOKABLE void toggleTags() { tagsHidden=!tagsHidden; refresh(false); notify(QAccessible::VisibleDataChanged); }
    void ensureVisible(int row) { if(row<0)return; if(row<top)top=row; if(row>=top+pageRows())top=row-pageRows()+1; }
    void choose(int row, Qt::KeyboardModifiers modifiers, int column=0, bool click=false);
    void focusCell(int id,int column);
    QRectF localCell(int row,int column) const {
        if(row<top||row>=top+pageRows()||row<0||row>=order.size())return {};
        const double x[5]={0,85,200,315,width()};
        return QRectF(x[column],HeaderHeight+(row-top)*RowHeight,qMax(0.0,x[column+1]-x[column]),RowHeight);
    }
    void paint(QPainter *p) override {
        p->fillRect(boundingRect(),QColor("#222630")); p->setFont(QFont("Segoe UI",10));
        p->fillRect(QRectF(0,0,width(),HeaderHeight),QColor("#303746"));
        const double x[4]={0,85,200,315};
        p->setPen(QColor("#d7deeb")); for(int c=0;c<Columns;++c)p->drawText(QRectF(x[c]+7,0,(c==3?width():x[c+1])-x[c]-10,HeaderHeight),Qt::AlignVCenter,header(c));
        int count=0;
        for(int row=top;row<order.size()&&row<top+pageRows();++row){
            ++count; int id=order[row]; QRectF full(0,HeaderHeight+(row-top)*RowHeight,width(),RowHeight);
            if(selected.contains(id))p->fillRect(full,QColor("#254b6a")); else if(row%2)p->fillRect(full,QColor("#262b35"));
            p->setPen(QColor("#ecf0f6"));
            for(int c=0;c<Columns;++c){auto box=localCell(row,c).adjusted(7,0,-5,0);p->drawText(box,Qt::AlignVCenter|Qt::AlignLeft,value(id,c));}
            if(id==current){p->setPen(QPen(QColor("#86c8ff"),1));p->drawRect(full.adjusted(.5,.5,-.5,-.5));}
        }
        maxPaintRows=qMax(maxPaintRows,count);
        if(order.isEmpty()){p->setPen(QColor("#d7deeb"));p->drawText(boundingRect(),Qt::AlignCenter,"No displayed Lines; current and selected IDs retained");}
    }
protected:
    void keyPressEvent(QKeyEvent *event) override {
        int row=index(current); if(order.isEmpty()){event->accept();return;}
        switch(event->key()) {
        case Qt::Key_Home:row=0;break;case Qt::Key_End:row=order.size()-1;break;
        case Qt::Key_Up:row=qMax(0,row-1);break;case Qt::Key_Down:row=qMin(int(order.size())-1,row+1);break;
        case Qt::Key_PageUp:row=qMax(0,row-pageRows());break;case Qt::Key_PageDown:row=qMin(int(order.size())-1,row+pageRows());break;
        default:QQuickPaintedItem::keyPressEvent(event);return;
        }
        choose(row,event->modifiers()); event->accept();
    }
    void mousePressEvent(QMouseEvent *event) override {
        int row=top+int((event->position().y()-HeaderHeight)/RowHeight);
        if(event->position().y()>=HeaderHeight&&row>=0&&row<order.size())choose(row,event->modifiers(),0,true);
        event->accept();
    }
    void wheelEvent(QWheelEvent *event) override { top=qBound(0,top-event->angleDelta().y()/40,qMax(0,int(order.size())-pageRows()));refresh(false);notify(QAccessible::LocationChanged);event->accept(); }
signals: void changed();
};

class CellAccessible : public QAccessibleInterface, public QAccessibleTableCellInterface, public QAccessibleActionInterface {
public:
    QPointer<PaintedGrid> grid;
    int line, column;
    CellAccessible(PaintedGrid *g,int id,int col):grid(g),line(id),column(col){++cellsCreated;}
    ~CellAccessible() override{++cellsDestroyed;}
    bool isValid()const override{return grid && line>0 && line<=RowTotal;}
    QObject *object()const override{return nullptr;} // Virtual cells are not paint objects or QML delegates.
    QWindow *window()const override{return grid?grid->window():nullptr;}
    QAccessibleInterface *parent()const override{return grid?QAccessible::queryAccessibleInterface(grid):nullptr;}
    QAccessibleInterface *child(int)const override{return nullptr;}
    QAccessibleInterface *childAt(int,int)const override{return nullptr;}
    int childCount()const override{return 0;}
    int indexOfChild(const QAccessibleInterface*)const override{return -1;}
    QString text(QAccessible::Text type)const override {
        if(!grid)return {};
        if(type==QAccessible::Name)return header(column)+": "+grid->value(line,column);
        if(type==QAccessible::Value)return grid->value(line,column);
        if(type==QAccessible::Description)return QString("Stable Line %1; displayed row %2; read-only synthetic value").arg(line).arg(rowIndex()+1);
        return {};
    }
    void setText(QAccessible::Text,const QString&)override{}
    QRect rect()const override {
        if(!grid||!grid->window())return {};
        auto r=grid->localCell(rowIndex(),column);if(r.isEmpty())return {};
        auto origin=grid->window()->mapToGlobal(grid->mapToScene(r.topLeft()).toPoint());return QRect(origin,r.size().toSize());
    }
    QAccessible::Role role()const override{return QAccessible::Cell;}
    QAccessible::State state()const override {
        QAccessible::State s;s.readOnly=true;s.selectable=true;s.focusable=true;
        s.invalid=!isValid();s.selected=isSelected();s.invisible=!grid||rowIndex()<0;s.offscreen=rect().isEmpty();
        s.focused=grid&&grid->hasActiveFocus()&&grid->current==line&&grid->currentColumn==column&&rowIndex()>=0;return s;
    }
    void *interface_cast(QAccessible::InterfaceType type)override {
        if(type==QAccessible::TableCellInterface)return static_cast<QAccessibleTableCellInterface*>(this);
        if(type==QAccessible::ActionInterface)return static_cast<QAccessibleActionInterface*>(this);return nullptr;
    }
    bool isSelected()const override{return grid&&grid->selected.contains(line)&&rowIndex()>=0;}
    QList<QAccessibleInterface*> columnHeaderCells()const override{return {};}
    QList<QAccessibleInterface*> rowHeaderCells()const override{return {};}
    int columnIndex()const override{return column;}
    int rowIndex()const override{return grid?grid->index(line):-1;}
    int columnExtent()const override{return 1;}
    int rowExtent()const override{return 1;}
    QAccessibleInterface *table()const override{return parent();}
    QStringList actionNames()const override{return {setFocusAction(),pressAction(),toggleAction()};}
    void doAction(const QString &name)override {
        if(!grid||rowIndex()<0)return;
        if(name==setFocusAction())grid->focusCell(line,column);
        else if(name==pressAction())grid->choose(rowIndex(),Qt::NoModifier,column);
        else if(name==toggleAction()){if(grid->selected.contains(line))grid->selected.remove(line);else grid->selected.insert(line);grid->refresh();}
    }
    QStringList keyBindingsForAction(const QString&)const override{return {};}
};

class GridAccessible : public QAccessibleObject, public QAccessibleTableInterface, public QAccessibleSelectionInterface {
public:
    QPointer<PaintedGrid> grid;
    mutable QHash<quint64,QAccessible::Id> cache;
    GridAccessible(PaintedGrid *g):QAccessibleObject(g),grid(g){}
    ~GridAccessible()override{for(auto id:cache)QAccessible::deleteAccessibleInterface(id);}
    QAccessibleInterface *cellAt(int row,int col)const override {
        if(!grid||row<0||row>=grid->order.size()||col<0||col>=Columns)return nullptr;
        int line=grid->order[row];quint64 key=quint64(line)*Columns+col;
        auto found=cache.constFind(key);if(found!=cache.cend())return QAccessible::accessibleInterface(*found);
        auto cell=new CellAccessible(grid,line,col);auto id=QAccessible::registerAccessibleInterface(cell);cache.insert(key,id);return cell;
    }
    int childCount()const override{return rowCount()*Columns;}
    QAccessibleInterface *child(int index)const override{return index<0?nullptr:cellAt(index/Columns,index%Columns);}
    int indexOfChild(const QAccessibleInterface *child)const override {
        auto cell=dynamic_cast<const CellAccessible*>(child);return cell&&cell->grid==grid&&cell->rowIndex()>=0?cell->rowIndex()*Columns+cell->column:-1;
    }
    QAccessibleInterface *parent()const override{return grid&&grid->window()?QAccessible::queryAccessibleInterface(grid->window()):nullptr;}
    QWindow *window()const override{return grid?grid->window():nullptr;}
    QAccessibleInterface *focusChild()const override{return grid&&grid->hasActiveFocus()?cellAt(grid->index(grid->current),grid->currentColumn):nullptr;}
    QAccessibleInterface *childAt(int x,int y)const override {
        if(!grid||!grid->window())return nullptr;
        QPointF p=grid->mapFromScene(grid->window()->mapFromGlobal(QPoint(x,y)));
        if(p.x()<0||p.x()>=grid->width()||p.y()<HeaderHeight||p.y()>=grid->height())return nullptr;
        int col=p.x()<85?0:p.x()<200?1:p.x()<315?2:3;return cellAt(grid->top+int((p.y()-HeaderHeight)/RowHeight),col);
    }
    QRect rect()const override{return grid&&grid->window()?QRect(grid->window()->mapToGlobal(grid->mapToScene(QPointF()).toPoint()),grid->size().toSize()):QRect();}
    QString text(QAccessible::Text type)const override {
        if(type==QAccessible::Name)return "Subtitle Grid";
        if(type==QAccessible::Description)return grid?grid->stateText():"Destroyed Grid";return {};
    }
    QAccessible::Role role()const override{return QAccessible::Table;}
    QAccessible::State state()const override{QAccessible::State s;s.focusable=true;s.focused=grid&&grid->hasActiveFocus();s.multiSelectable=true;s.extSelectable=true;s.readOnly=true;return s;}
    void *interface_cast(QAccessible::InterfaceType type)override {
        if(type==QAccessible::TableInterface)return static_cast<QAccessibleTableInterface*>(this);
        if(type==QAccessible::SelectionInterface)return static_cast<QAccessibleSelectionInterface*>(this);return nullptr;
    }
    QAccessibleInterface *caption()const override{return nullptr;}
    QAccessibleInterface *summary()const override{return nullptr;}
    QString columnDescription(int col)const override{return header(col);}
    QString rowDescription(int row)const override{return grid&&row>=0&&row<rowCount()?QString("Line %1").arg(grid->order[row]):QString();}
    int columnCount()const override{return Columns;}
    int rowCount()const override{return grid?grid->order.size():0;}
    QList<int> selectedRows()const override {QList<int> rows;if(grid)for(int id:grid->selected){int row=grid->index(id);if(row>=0)rows.append(row);}std::sort(rows.begin(),rows.end());return rows;}
    int selectedRowCount()const override{return selectedRows().size();}
    int selectedCellCount()const override{return selectedRowCount()*Columns;}
    QList<QAccessibleInterface*> selectedCells()const override{QList<QAccessibleInterface*> result;for(int row:selectedRows())for(int c=0;c<Columns;++c)result.append(cellAt(row,c));return result;}
    int selectedColumnCount()const override{return 0;}
    QList<int> selectedColumns()const override{return {};}
    bool isColumnSelected(int)const override{return false;}
    bool isRowSelected(int row)const override{return grid&&row>=0&&row<rowCount()&&grid->selected.contains(grid->order[row]);}
    bool selectRow(int row)override{if(!grid||row<0||row>=rowCount())return false;grid->selected.insert(grid->order[row]);grid->refresh();return true;}
    bool unselectRow(int row)override{if(!grid||row<0||row>=rowCount())return false;grid->selected.remove(grid->order[row]);grid->refresh();return true;}
    bool selectColumn(int)override{return false;}
    bool unselectColumn(int)override{return false;}
    void modelChange(QAccessibleTableModelChangeEvent*)override{} // Source remains fixed; mapping lives in PaintedGrid.
    int selectedItemCount()const override{return selectedCellCount();}
    QList<QAccessibleInterface*> selectedItems()const override{return selectedCells();}
    bool select(QAccessibleInterface *child)override{auto c=dynamic_cast<CellAccessible*>(child);return c&&c->grid==grid?selectRow(c->rowIndex()):false;}
    bool unselect(QAccessibleInterface *child)override{auto c=dynamic_cast<CellAccessible*>(child);return c&&c->grid==grid?unselectRow(c->rowIndex()):false;}
    bool selectAll()override{if(!grid)return false;for(int id:grid->order)grid->selected.insert(id);grid->refresh();return true;}
    bool clear()override{if(!grid)return false;for(int id:grid->order)grid->selected.remove(id);grid->refresh();return true;}
};
static QAccessibleInterface *factory(const QString&,QObject *object){if(auto grid=qobject_cast<PaintedGrid*>(object))return new GridAccessible(grid);return nullptr;}
void PaintedGrid::focusCell(int id,int col){if(index(id)<0)return;current=id;currentColumn=col;forceActiveFocus();ensureVisible(index(id));refresh(false);auto root=QAccessible::queryAccessibleInterface(this);auto cell=root->tableInterface()->cellAt(index(id),col);QAccessibleEvent event(cell,QAccessible::Focus);QAccessible::updateAccessibility(&event);}
void PaintedGrid::choose(int row,Qt::KeyboardModifiers modifiers,int col,bool click){
    if(row<0||row>=order.size())return;int id=order[row];
    if(modifiers.testFlag(Qt::ShiftModifier)){
        int start=index(anchor);if(start<0)start=row;if(!modifiers.testFlag(Qt::ControlModifier))selected.clear();
        for(int i=qMin(start,row);i<=qMax(start,row);++i)selected.insert(order[i]);
    }else if(modifiers.testFlag(Qt::ControlModifier)){if(click){if(selected.contains(id))selected.remove(id);else selected.insert(id);anchor=id;}}
    else{selected={id};anchor=id;}
    focusCell(id,col);refresh();
}
static QJsonArray sortedIds(const QSet<int>& set){auto ids=set.values();std::sort(ids.begin(),ids.end());QJsonArray array;for(int id:ids)array.append(id);return array;}
static QJsonObject state(PaintedGrid *grid,GridAccessible *root){return {{"displayed",grid->order.size()},{"current",grid->current},{"current_row",grid->index(grid->current)},{"selected_ids",sortedIds(grid->selected)},{"hidden_selected",grid->hiddenSelected()},{"anchor",grid->anchor},{"accessible_selected_rows",root->selectedRowCount()},{"cached_cells",root->cache.size()},{"active_focus",grid->hasActiveFocus()}};}
static void runObservations(QQuickWindow *window,PaintedGrid *grid,QString output){
    QJsonArray observations;bool matched=true;
    auto root=dynamic_cast<GridAccessible*>(QAccessible::queryAccessibleInterface(grid));
    auto record=[&](QString name,bool pass,QJsonObject detail=QJsonObject()){
        matched &= pass;observations.append(QJsonObject{{"case",name},{"matched",pass},{"detail",detail}});
    };
    record("Real C++ table and selection interfaces",root&&root->tableInterface()&&root->selectionInterface());
    if(!root){QCoreApplication::exit(2);return;}
    record("50k rows, four columns, lazy cells",root->rowCount()==50000&&root->columnCount()==4&&root->childCount()==200000&&root->cache.size()<20,state(grid,root));
    auto last=root->cellAt(49999,3);auto lastUid=QAccessible::uniqueId(last);
    record("Virtual offscreen cell has identity and table-cell interface",last->tableCellInterface()&&last->tableCellInterface()->rowIndex()==49999&&last->state().offscreen&&last->rect().isEmpty(),{{"id",double(lastUid)},{"name",last->text(QAccessible::Name)},{"cached_cells",root->cache.size()}});
    record("Repeat lookup preserves interface identity",last==root->cellAt(49999,3)&&lastUid==QAccessible::uniqueId(root->cellAt(49999,3)));
    record("Invalid lookup rejected",!root->cellAt(-1,0)&&!root->cellAt(50000,0)&&!root->cellAt(0,4));
    root->clear();root->selectRow(4);auto selectedCell=root->cellAt(4,2);auto selectedUid=QAccessible::uniqueId(selectedCell);
    record("Row selection projects four cells",root->selectedCellCount()==4&&root->selectedCells().size()==4&&selectedCell->tableCellInterface()->isSelected(),state(grid,root));
    last->actionInterface()->doAction(QAccessibleActionInterface::setFocusAction());
    record("Accessible focus action moves current without selection",grid->current==50000&&grid->selected==QSet<int>{5}&&root->focusChild()==last&&last->state().focused,state(grid,root));
    auto pos=last->rect().center();record("Visible cell screen rect resolves through childAt",!last->rect().isEmpty()&&root->childAt(pos.x(),pos.y())==last);
    root->selectionInterface()->select(root->cellAt(7,1));record("Selection interface adds a row",grid->selected==QSet<int>({5,8})&&root->selectedItemCount()==8,state(grid,root));
    grid->reverseRows();
    record("Sort changes row position, not Line/cell identity",root->cellAt(49995,2)==selectedCell&&QAccessible::uniqueId(selectedCell)==selectedUid&&selectedCell->tableCellInterface()->rowIndex()==49995,state(grid,root));
    grid->filterRows("rtl");record("Hidden selection retained outside exposed rows",grid->hiddenSelected()==2&&root->selectedRowCount()==0&&selectedCell->tableCellInterface()->rowIndex()==-1&&selectedCell->state().invisible&&root->indexOfChild(selectedCell)==-1,state(grid,root));
    auto key=[&](int k,Qt::KeyboardModifiers mod=Qt::NoModifier){QKeyEvent press(QEvent::KeyPress,k,mod),release(QEvent::KeyRelease,k,mod);grid->forceActiveFocus();QCoreApplication::sendEvent(window,&press);QCoreApplication::sendEvent(window,&release);QCoreApplication::processEvents();};
    key(Qt::Key_Home);record("Dispatched Home follows filtered reverse order",grid->current==49998&&grid->selected==QSet<int>{49998},state(grid,root));
    key(Qt::Key_End,Qt::ControlModifier);record("Dispatched Ctrl-End retains selection/anchor",grid->current==3&&grid->selected==QSet<int>{49998}&&grid->anchor==49998,state(grid,root));
    key(Qt::Key_Home,Qt::ShiftModifier);key(Qt::Key_Down,Qt::ShiftModifier);record("Dispatched Shift range follows displayed IDs",grid->selected==QSet<int>({49998,49995})&&grid->current==49995,state(grid,root));
    grid->filterRows("empty");key(Qt::Key_End);record("Empty view retains current/hidden selection",root->rowCount()==0&&root->childCount()==0&&!root->focusChild()&&grid->selected.size()==2&&grid->current==49995,state(grid,root));
    grid->filterRows("all");record("Restore reuses queried cell identity",root->cellAt(49995,2)==selectedCell&&selectedCell->tableCellInterface()->rowIndex()==49995);
    auto textCell=root->cellAt(grid->index(3),3);QString raw=rawText(3),before=textCell->text(QAccessible::Value);grid->toggleTags();
    record("Displayed accessible text changes without raw mutation",before.contains("\\i1")&&!textCell->text(QAccessible::Value).contains("\\i1")&&rawText(3)==raw,{{"raw",raw},{"displayed",textCell->text(QAccessible::Value)}});
    textCell->actionInterface()->doAction(QAccessibleActionInterface::pressAction());record("Accessible press selects and focuses exact Line",grid->selected==QSet<int>{3}&&grid->current==3&&textCell->state().focused,state(grid,root));
    textCell->actionInterface()->doAction(QAccessibleActionInterface::toggleAction());record("Accessible toggle leaves current while clearing selected Line",grid->current==3&&!grid->selected.contains(3),state(grid,root));
    record("Column descriptions supplied, column selection explicitly unsupported",root->columnDescription(3)=="Subtitle text"&&!root->selectColumn(3));
    int createdBefore=cellsCreated,destroyedBefore=cellsDestroyed;auto temporary=new PaintedGrid;auto other=dynamic_cast<GridAccessible*>(QAccessible::queryAccessibleInterface(temporary));
    QList<QAccessible::Id> removed;for(int row:{0,20000,49999})removed.append(QAccessible::uniqueId(other->cellAt(row,3)));
    delete temporary;QCoreApplication::processEvents();bool released=true;for(auto id:removed)released &= QAccessible::accessibleInterface(id)==nullptr;
    record("Grid destruction releases registered lazy cells",released&&cellsCreated-createdBefore==3&&cellsDestroyed-destroyedBefore==3,{{"created",cellsCreated-createdBefore},{"destroyed",cellsDestroyed-destroyedBefore}});
    QSet<int> eventTypes;for(const auto &event:interfaceEvents)eventTypes.insert(event.toObject()["type"].toInt());
    record("In-process update handler received model, selection and focus events",eventTypes.contains(QAccessible::TableModelChanged)&&eventTypes.contains(QAccessible::SelectionWithin)&&eventTypes.contains(QAccessible::Focus),{{"received_events",interfaceEvents.size()}});
    grid->filterRows("all");if(grid->reversed)grid->reverseRows();grid->choose(4,Qt::NoModifier);QCoreApplication::processEvents();
    QCryptographicHash fixture(QCryptographicHash::Sha256);qint64 fixtureBytes=0;
    for(int id=1;id<=RowTotal;++id){QByteArray bytes=(QString::number(id)+"\t"+rawText(id)+(id<RowTotal?"\n":"")).toUtf8();fixture.addData(bytes);fixtureBytes+=bytes.size();}
    QString compiler;
#ifdef _MSC_FULL_VER
    compiler=QString("MSVC %1").arg(_MSC_FULL_VER);
#else
    compiler=__VERSION__;
#endif
    QJsonObject origins;
#ifdef Q_OS_WIN
    for(const wchar_t *name:{L"Qt6Core.dll",L"Qt6Gui.dll",L"Qt6Quick.dll",L"Qt6Qml.dll"}){
        wchar_t location[32768]={};auto module=GetModuleHandleW(name);
        if(module&&GetModuleFileNameW(module,location,32768))origins.insert(QString::fromWCharArray(name),QString::fromWCharArray(location));
    }
#endif
    QJsonObject report{{"qt_version",qVersion()},{"compiler",compiler},{"loaded_qt_paths",origins},{"application_fonts",applicationFonts},{"platform_plugin",QGuiApplication::platformName()},{"os",QSysInfo::prettyProductName()},{"observations",observations},{"all_observations_matched",matched},{"interface_events",interfaceEvents},{"source_row_count",RowTotal},{"fixture_sha256",QString(fixture.result().toHex())},{"fixture_bytes",double(fixtureBytes)},{"cached_cells_at_end",root->cache.size()},{"cells_created",cellsCreated},{"cells_destroyed",cellsDestroyed},{"max_rows_painted_in_one_callback",grid->maxPaintRows},{"uia_observed",false},{"screen_reader_observed",false},{"performance_gate_measured",false},{"accessible_update_handler","in-process replacement; not OS event delivery"}};
    QDir().mkpath(output);QFile file(output+"/observations.json");file.open(QIODevice::WriteOnly);file.write(QJsonDocument(report).toJson());file.close();
    // Own Qt scene capture only; never desktop/UI automation.
    QTimer::singleShot(250,window,[window,output,matched](){bool captured=window->grabWindow().save(output+"/grid.png");QFile f(output+"/capture.json");f.open(QIODevice::WriteOnly);f.write(QJsonDocument(QJsonObject{{"own_window_capture_saved",captured},{"physical_display_claim",false}}).toJson());QCoreApplication::exit(matched?0:3);});
}
int main(int argc,char **argv){
    QGuiApplication app(argc,argv);QAccessible::installFactory(factory);
    // Offscreen Qt has no Windows system font discovery. Read installed fonts into this process only.
    // No font bytes are copied into the repository or registered with the operating system.
    if(QGuiApplication::platformName()=="offscreen"){
        for(const QString &name:{"segoeui.ttf","arial.ttf","msgothic.ttc"}){
            QString path="C:/Windows/Fonts/"+name;QFile font(path);
            if(font.open(QIODevice::ReadOnly)){
                auto bytes=font.readAll();int id=QFontDatabase::addApplicationFontFromData(bytes);
                applicationFonts.append(QJsonObject{{"source",path},{"sha256",QString(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex())},{"qt_application_font_id",id}});
            }
        }
    }
    QGuiApplication::setFont(QFont("Segoe UI",10));
    bool observe=app.arguments().contains("--observe");QString output=QStringLiteral(PROBE_SOURCE_DIR)+"/_run/latest";
    int at=app.arguments().indexOf("--output");if(at>=0&&at+1<app.arguments().size())output=app.arguments()[at+1];
    if(observe){QAccessible::setActive(true);QAccessible::installUpdateHandler(observeEvent);}
    qmlRegisterType<PaintedGrid>("HikariProbe",1,0,"PaintedGrid");QQmlApplicationEngine engine;
    engine.load(QUrl::fromLocalFile(QStringLiteral(PROBE_SOURCE_DIR)+"/Main.qml"));if(engine.rootObjects().isEmpty())return 2;
    auto window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());auto grid=window->findChild<PaintedGrid*>("probeGrid");
    if(observe)QTimer::singleShot(600,&app,[=](){runObservations(window,grid,output);});
    return app.exec();
}
#include "main.moc"
