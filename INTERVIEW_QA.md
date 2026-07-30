# Qt 相册项目面试问答

## 一、项目整体架构

### 相册项目整体架构简单说一遍

**答：** 这个相册项目采用经典的 **MVC（Model-View-Controller）** 架构模式，主要分为以下几个层次：

```
┌─────────────────────────────────────────────────────────┐
│                    MainWindow (主窗口)                    │
│  ┌──────────────┐  ┌──────────────────────────────────┐  │
│  │   ProTree    │  │          PicShow (图片展示)        │  │
│  │  (项目树)    │  │  ┌────────────────────────────┐  │  │
│  │              │  │  │      QLabel (图片显示)      │  │  │
│  │  TreeWidget  │  │  └────────────────────────────┘  │  │
│  │              │  │  ┌──────┐          ┌──────┐      │  │
│  │              │  │  │ 上一张│          │ 下一张│      │  │
│  └──────────────┘  │  └──────┘          └──────┘      │  │
│                    └──────────────────────────────────┘  │
├─────────────────────────────────────────────────────────┤
│                    数据层 (Model)                         │
│  ┌──────────────────┐  ┌──────────────────────────────┐ │
│  │   ProTreeItem    │  │      ProTreeThread            │ │
│  │  (树节点数据)    │  │    (文件扫描线程)              │ │
│  └──────────────────┘  └──────────────────────────────┘ │
│  ┌──────────────────┐  ┌──────────────────────────────┐ │
│  │   OpenTreeThread │  │      SlideShowDlg             │ │
│  │  (打开项目线程)  │  │    (幻灯片展示)               │ │
│  └──────────────────┘  └──────────────────────────────┘ │
└─────────────────────────────────────────────────────────┘
```

**核心组件：**

1. **MainWindow** - 主窗口，负责菜单栏、工具栏和整体布局
2. **ProTree** - 项目树容器，包含 QTreeWidget
3. **ProTreeWidget** - 自定义树控件，管理项目节点和右键菜单
4. **ProTreeItem** - 树节点数据类，存储文件路径和前后节点关系
5. **PicShow** - 图片展示窗口，支持前后翻页和动画
6. **ProTreeThread/OpenTreeThread** - 子线程，负责文件扫描和复制
7. **SlideShowDlg** - 幻灯片展示对话框，带动画和音乐

**数据流：**
- 用户创建/打开项目 → Wizard → ProTreeWidget → 子线程扫描文件 → 构建树结构
- 用户双击图片 → ProTreeWidget → SigUpdateSelected → PicShow 显示图片
- 用户点击上一张/下一张 → PicShow → SigPreClicked/SigNextClicked → ProTreeWidget 切换节点

---

## 二、核心技术问题

### 文件树递归扫描，为什么放到子线程，如果不放到子线程会怎么样

**答：**

**为什么放到子线程：**
1. **避免界面卡顿** - 文件系统操作（遍历目录、复制文件）是 I/O 密集型操作，耗时较长
2. **保持响应性** - 主线程负责 UI 渲染和事件处理，不能被阻塞
3. **用户体验** - 可以显示进度条，用户可以取消操作

**如果不放到子线程会怎么样：**
1. **界面冻结** - 主线程被阻塞，窗口无法响应鼠标、键盘事件
2. **无法显示进度** - 进度条无法更新，用户看不到扫描进度
3. **无法取消** - 用户点击取消按钮无效，因为事件循环被阻塞
4. **系统判定程序无响应** - Windows 会弹出"程序未响应"对话框

**代码示例：**
```cpp
// 子线程中执行
void ProTreeThread::run() {
    CreateProTree(_src_path, _dist_path, _parent_item, _file_count, _self, _root);
    if(!_bstop) {
        emit SigFinishProgress(_file_count);
    }
}

// 主线程中启动
_thread_create_pro = std::make_shared<ProTreeThread>(...);
connect(_thread_create_pro.get(), &ProTreeThread::SigUpdateProgress,
        this, &ProTreeWidget::SlotUpdateProgress);
_thread_create_pro->start();
_dialog_progress->exec(); // 模态对话框，但事件循环仍在运行
```

---

### 子线程扫描完成大量文件数据，怎么刷新界面，中间遇到什么 bug

**答：**

**刷新界面的方式：**
使用 **信号槽机制（Signal-Slot）**，子线程发射信号，主线程槽函数更新 UI。

```cpp
// 子线程发射信号
void ProTreeThread::CreateProTree(...) {
    file_count++;
    emit SigUpdateProgress(file_count); // 通知主线程更新进度
}

// 主线程槽函数
void ProTreeWidget::SlotUpdateProgress(int count) {
    if(!_dialog_progress) return;
    _dialog_progress->setValue(count % PROGRESS_MAX);
}
```

**遇到的 Bug：**

1. **Bug 1：子线程直接操作 UI 崩溃**
   - **现象**：子线程中直接调用 `QTreeWidget::addTopLevelItem()` 导致程序崩溃
   - **原因**：Qt 要求所有 UI 操作必须在主线程执行
   - **解决**：子线程只发射信号，主线程槽函数中操作 UI

2. **Bug 2：进度条不更新**
   - **现象**：扫描文件时进度条不动
   - **原因**：`_dialog_progress->exec()` 是模态对话框，但信号槽连接在 `exec()` 之前
   - **解决**：先 `start()` 线程，再 `exec()` 对话框，确保信号槽已连接

3. **Bug 3：取消操作后程序崩溃**
   - **现象**：点击取消按钮后程序崩溃
   - **原因**：子线程仍在运行，访问已删除的 `_dialog_progress`
   - **解决**：使用 `_bstop` 标志位，子线程检查后安全退出

4. **Bug 4：跨线程访问共享数据**
   - **现象**：多线程同时修改 `_file_count` 导致数据不一致
   - **原因**：没有加锁保护
   - **解决**：使用 `std::atomic<int>` 或 `QMutex`

---

### 大量文件加载，界面卡顿怎么优化

**答：**

**优化策略：**

1. **懒加载（Lazy Loading）**
   - 只在需要时才加载图片，不预先加载所有图片
   - 双击图片时才加载到内存

2. **图片缩略图缓存**
   - 生成缩略图缓存，避免每次都读取原图
   - 使用 `QPixmap::scaled()` 生成缩略图

3. **虚拟滚动**
   - 只渲染可见区域的树节点
   - 使用 `QTreeWidget::setUniformRowHeights(true)` 优化

4. **批量更新**
   - 子线程扫描时，每 100 个文件发射一次信号，而不是每个文件都发射
   - 减少信号槽调用次数

5. **异步图片加载**
   - 使用 `QFutureWatcher` 异步加载图片
   - 图片加载完成后更新 UI

6. **内存管理**
   - 及时释放不需要的 `QPixmap`
   - 使用 `QPixmapCache` 缓存常用图片

**代码示例：**
```cpp
// 批量更新优化
void ProTreeThread::CreateProTree(...) {
    for(int i = 0; i < list.size(); i++) {
        // ... 处理文件
        file_count++;
        if(file_count % 100 == 0) { // 每100个文件更新一次
            emit SigUpdateProgress(file_count);
        }
    }
}
```

---

### Model-View 你是怎么用的，为什么不用简单的 QTreeWidgetItem

**答：**

**项目中的 Model-View 使用：**

1. **ProTreeWidget 继承 QTreeWidget**
   - QTreeWidget 内部使用了 Model-View 架构
   - `QTreeWidget` = `QTreeView` + `QTreeWidgetItemModel`
   - 我们通过 `QTreeWidgetItem` 来管理数据

2. **自定义 ProTreeItem**
   - 继承 `QTreeWidgetItem`，添加自定义数据（路径、前后节点指针）
   - 重写 `type()` 方法区分不同节点类型（项目/文件夹/图片）

**为什么不用简单的 QTreeWidgetItem：**

1. **需要自定义数据**
   - `QTreeWidgetItem` 只支持 `setData()` 存储通用数据
   - 我们需要存储文件路径、前后节点关系等自定义数据
   - 继承后可以添加成员变量，更方便管理

2. **需要类型区分**
   - 使用 `type()` 方法区分项目、文件夹、图片节点
   - 右键菜单根据节点类型显示不同选项

3. **需要链表结构**
   - 图片节点需要前后指针，支持上一张/下一张功能
   - `QTreeWidgetItem` 不支持这种链表结构

4. **代码更清晰**
   - 继承后可以把相关逻辑封装在 `ProTreeItem` 中
   - 避免在 `ProTreeWidget` 中写大量类型转换代码

**如果用 QAbstractItemModel：**
- 更灵活，但实现复杂
- 适合数据量大、需要自定义视图的场景
- 这个项目数据量不大，`QTreeWidget` 足够

---

### 如何保证线程安全，共享数据怎么保护

**答：**

**共享数据：**
1. `_file_count` - 文件计数器，子线程写，主线程读
2. `_bstop` - 停止标志，主线程写，子线程读
3. `_right_btn_item` - 右键选中的节点，主线程操作

**线程安全保护方法：**

1. **信号槽机制（主要方式）**
   - 子线程通过信号通知主线程，主线程槽函数更新 UI
   - Qt 保证信号槽跨线程调用的安全性（使用事件队列）

```cpp
// 子线程发射信号
emit SigUpdateProgress(file_count);

// 主线程槽函数（自动在主线程执行）
void ProTreeWidget::SlotUpdateProgress(int count) {
    _dialog_progress->setValue(count);
}
```

2. **原子变量**
   - `_bstop` 使用 `bool`，但需要确保原子性
   - 可以使用 `std::atomic<bool>`

```cpp
// 子线程检查
void ProTreeThread::CreateProTree(...) {
    if(_bstop) return; // 读取停止标志
    // ...
}

// 主线程设置
void ProTreeThread::SlotCancelProgress() {
    _bstop = true; // 写入停止标志
}
```

3. **互斥锁（QMutex）**
   - 如果有多个线程同时修改共享数据，需要加锁
   - 这个项目中主要是单向通信（子线程→主线程），所以不需要

```cpp
// 如果需要加锁
QMutex mutex;
int _file_count;

void ProTreeThread::CreateProTree(...) {
    QMutexLocker locker(&mutex);
    file_count++;
    emit SigUpdateProgress(file_count);
}
```

4. **避免共享数据**
   - 尽量让子线程只负责计算，主线程负责更新
   - 子线程通过信号传递数据，而不是直接修改共享变量

---

### 项目里面最难解决的问题是什么，如何定位，如何修复

**答：**

**最难解决的问题：子线程操作 UI 导致的随机崩溃**

**问题现象：**
- 程序随机崩溃，没有固定复现步骤
- 崩溃位置不固定，有时在 `QTreeWidget::addTopLevelItem()`
- 有时在 `QProgressDialog::setValue()`

**定位过程：**

1. **查看崩溃堆栈**
   - 使用 Qt Creator 的调试器，查看崩溃时的调用栈
   - 发现崩溃在 UI 操作函数中

2. **添加日志**
   - 在子线程和主线程的关键位置添加 `qDebug()`
   - 发现子线程在 UI 操作时，主线程也在操作 UI

3. **使用线程分析工具**
   - 使用 `Valgrind` 的 `Helgrind` 工具检测线程竞争
   - 发现多处数据竞争

**根本原因：**
- 子线程直接调用了 `QTreeWidget::addTopLevelItem()`
- Qt 的 UI 操作必须在主线程执行
- 子线程操作 UI 会导致未定义行为

**修复方案：**

1. **子线程只发射信号**
```cpp
// 修复前（错误）
void ProTreeThread::CreateProTree(...) {
    auto * item = new ProTreeItem(parent_item, ...);
    parent_item->addChild(item); // 子线程操作UI，崩溃！
}

// 修复后（正确）
void ProTreeThread::CreateProTree(...) {
    auto * item = new ProTreeItem(parent_item, ...);
    emit SigUpdateProgress(file_count); // 只发射信号
}
```

2. **主线程槽函数中操作 UI**
```cpp
void ProTreeWidget::SlotUpdateProgress(int count) {
    // 在主线程中操作UI
    _dialog_progress->setValue(count);
}
```

3. **使用 QMetaObject::invokeMethod**
```cpp
// 如果必须在子线程中触发UI更新
QMetaObject::invokeMethod(this, [this, count](){
    _dialog_progress->setValue(count);
}, Qt::QueuedConnection);
```

---

### 如果让你优化这个项目，你会做哪些改进

**答：**

**1. 架构优化**
- 使用 `QAbstractItemModel` 替代 `QTreeWidgetItem`，更灵活
- 引入 MVC 分离，Model 层独立管理数据
- 使用 `QThreadPool` 替代手动管理 `QThread`

**2. 性能优化**
- 实现图片懒加载，只加载可见区域
- 添加缩略图缓存，避免重复生成
- 使用 `QPixmapCache` 缓存常用图片
- 批量更新 UI，减少信号槽调用

**3. 功能增强**
- 添加图片编辑功能（裁剪、旋转、滤镜）
- 支持更多图片格式（RAW、HEIC）
- 添加图片搜索功能
- 支持标签分类
- 添加图片对比功能

**4. 用户体验**
- 添加拖拽支持
- 支持键盘快捷键
- 添加全屏预览模式
- 支持图片缩放
- 添加图片信息显示（EXIF 数据）

**5. 代码质量**
- 添加单元测试
- 使用智能指针管理内存
- 添加异常处理
- 完善日志系统
- 使用 CMake 替代 qmake

**6. 跨平台优化**
- 针对不同平台优化文件扫描速度
- 支持深色模式
- 适配高 DPI 屏幕

---

## 三、手写代码题

### 简单：递归遍历文件夹打印所有文件（QDir）

```cpp
void recursivePrint(const QString &path) {
    QDir dir(path);
    if (!dir.exists()) {
        qDebug() << "Directory does not exist:" << path;
        return;
    }

    // 设置过滤器：只显示文件和目录，不显示 . 和 ..
    dir.setFilter(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    dir.setSorting(QDir::Name);

    QFileInfoList list = dir.entryInfoList();
    for (int i = 0; i < list.size(); ++i) {
        QFileInfo fileInfo = list.at(i);

        if (fileInfo.isDir()) {
            // 是目录，递归遍历
            qDebug() << "[DIR] " << fileInfo.absoluteFilePath();
            recursivePrint(fileInfo.absoluteFilePath());
        } else {
            // 是文件
            qDebug() << "[FILE]" << fileInfo.absoluteFilePath();
        }
    }
}

// 使用示例
recursivePrint("C:/Users/test/Pictures");
```

---

### 简单：写一个自定义信号槽，跨线程发送字符串

```cpp
// worker.h
#ifndef WORKER_H
#define WORKER_H

#include <QObject>
#include <QThread>
#include <QString>

class Worker : public QObject
{
    Q_OBJECT
public:
    explicit Worker(QObject *parent = nullptr) : QObject(parent) {}

public slots:
    void doWork() {
        // 模拟耗时操作
        for (int i = 0; i < 5; ++i) {
            QThread::sleep(1);
            // 发射信号，跨线程传递字符串
            emit resultReady(QString("Result from thread: %1").arg(i));
        }
        emit finished();
    }

signals:
    void resultReady(const QString &result);
    void finished();
};

#endif // WORKER_H
```

```cpp
// main.cpp 或使用的地方
#include <QCoreApplication>
#include <QThread>
#include "worker.h"

int main(int argc, char *argv[]) {
    QCoreApplication a(argc, argv);

    QThread thread;
    Worker worker;

    // 将 worker 移动到子线程
    worker.moveToThread(&thread);

    // 连接信号槽（跨线程）
    QObject::connect(&thread, &QThread::started, &worker, &Worker::doWork);
    QObject::connect(&worker, &Worker::resultReady, [](const QString &result) {
        qDebug() << "Received:" << result;
    });
    QObject::connect(&worker, &Worker::finished, &thread, &QThread::quit);
    QObject::connect(&thread, &QThread::finished, &worker, &Worker::deleteLater);
    QObject::connect(&thread, &QThread::finished, &thread, &QThread::deleteLater);

    // 启动线程
    thread.start();

    return a.exec();
}
```

---

### 中等：简单继承 QAbstractListModel 实现 model

```cpp
// stringlistmodel.h
#ifndef STRINGLISTMODEL_H
#define STRINGLISTMODEL_H

#include <QAbstractListModel>
#include <QStringList>

class StringListModel : public QAbstractListModel
{
    Q_OBJECT
public:
    explicit StringListModel(const QStringList &strings, QObject *parent = nullptr)
        : QAbstractListModel(parent), m_strings(strings) {}

    // 必须实现的三个方法
    int rowCount(const QModelIndex &parent = QModelIndex()) const override {
        if (parent.isValid())
            return 0;
        return m_strings.size();
    }

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override {
        if (!index.isValid())
            return QVariant();

        if (index.row() >= m_strings.size())
            return QVariant();

        if (role == Qt::DisplayRole || role == Qt::EditRole) {
            return m_strings.at(index.row());
        }

        return QVariant();
    }

    // 可选：支持编辑
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override {
        if (index.isValid() && role == Qt::EditRole) {
            m_strings.replace(index.row(), value.toString());
            emit dataChanged(index, index, {role});
            return true;
        }
        return false;
    }

    Qt::ItemFlags flags(const QModelIndex &index) const override {
        if (!index.isValid())
            return Qt::NoItemFlags;
        return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
    }

    // 添加行
    bool insertRows(int row, int count, const QModelIndex &parent = QModelIndex()) override {
        beginInsertRows(parent, row, row + count - 1);
        for (int i = 0; i < count; ++i) {
            m_strings.insert(row, "");
        }
        endInsertRows();
        return true;
    }

    // 删除行
    bool removeRows(int row, int count, const QModelIndex &parent = QModelIndex()) override {
        beginRemoveRows(parent, row, row + count - 1);
        for (int i = 0; i < count; ++i) {
            m_strings.removeAt(row);
        }
        endRemoveRows();
        return true;
    }

private:
    QStringList m_strings;
};

#endif // STRINGLISTMODEL_H
```

```cpp
// 使用示例
#include <QApplication>
#include <QListView>
#include "stringlistmodel.h"

int main(int argc, char *argv[]) {
    QApplication a(argc, argv);

    QStringList strings;
    strings << "Item 1" << "Item 2" << "Item 3" << "Item 4";

    StringListModel model(strings);
    QListView view;
    view.setModel(&model);
    view.show();

    return a.exec();
}
```

---

## 四、通用问题

### 了解 Qt 跨平台原理吗

**答：**

**Qt 跨平台原理：**

1. **抽象层（Abstraction Layer）**
   - Qt 在不同平台上实现了统一的 API
   - 底层调用平台特定的 API（Windows API、POSIX、Cocoa 等）
   - 用户代码只需要调用 Qt API，不需要关心平台差异

2. **编译时适配**
   - Qt 根据编译平台选择对应的实现
   - 例如 `QDir` 在 Windows 上调用 `FindFirstFile`，在 Linux 上调用 `opendir`

3. **运行时适配**
   - 某些功能在运行时检测平台能力
   - 例如 `QStyle` 根据系统主题选择样式

4. **条件编译**
   - 使用 `#ifdef Q_OS_WIN` 等宏进行条件编译
   - 针对不同平台编写特定代码

**示例：**
```cpp
// Qt 内部实现示例
#ifdef Q_OS_WIN
// Windows 实现
bool QDir::exists() {
    return GetFileAttributesW(path.toStdWString().c_str()) != INVALID_FILE_ATTRIBUTES;
}
#elif defined(Q_OS_UNIX)
// Linux/Mac 实现
bool QDir::exists() {
    struct stat info;
    return stat(path.toStdString().c_str(), &info) == 0;
}
#endif
```

**跨平台注意事项：**
- 文件路径分隔符（Windows 用 `\`，Linux 用 `/`）
- 字符编码（Windows 用 UTF-16，Linux 用 UTF-8）
- 线程 API（Windows 用 `CreateThread`，Linux 用 `pthread`）
- 网络 API（Windows 用 Winsock，Linux 用 BSD Socket）

---

### 你平时怎么学习 Qt？文档还是博客

**答：**

**我的学习方式（组合使用）：**

1. **Qt 官方文档（主要）**
   - 每个类都有详细的文档和示例
   - 网址：https://doc.qt.io/qt-6/
   - 优点：权威、详细、有示例代码
   - 缺点：英文、比较长

2. **Qt 官方示例（重要）**
   - Qt Creator 中可以直接查看和运行示例
   - 路径：`Qt/Examples/`
   - 优点：可运行、代码完整
   - 缺点：示例可能过于简单

3. **博客和教程（辅助）**
   - CSDN、知乎、掘金上的 Qt 教程
   - 优点：中文、有实战经验
   - 缺点：质量参差不齐

4. **源码阅读（深入）**
   - 阅读 Qt 源码理解实现原理
   - 优点：深入理解
   - 缺点：难度大

5. **实践项目（最重要）**
   - 通过实际项目学习
   - 这个相册项目就是很好的实践

**学习建议：**
- 先看官方文档和示例，理解基本用法
- 遇到问题时查博客，看别人的解决方案
- 重要的是多写代码，实践出真知

---

### Git 使用，分支提交

**答：**

**Git 基本工作流：**

```bash
# 1. 克隆仓库
git clone <url>

# 2. 创建功能分支
git checkout -b feature/album-slideshow

# 3. 修改代码
# ... 编写代码 ...

# 4. 添加修改
git add .

# 5. 提交
git commit -m "feat: add slideshow feature"

# 6. 推送到远程
git push origin feature/album-slideshow

# 7. 创建 Pull Request
# 在 GitHub/GitLab 上创建 PR

# 8. 代码审查后合并
git checkout master
git merge feature/album-slideshow
git push origin master
```

**分支策略：**
- `master` - 主分支，稳定版本
- `develop` - 开发分支
- `feature/*` - 功能分支
- `hotfix/*` - 紧急修复分支

**提交规范（Conventional Commits）：**
- `feat:` - 新功能
- `fix:` - 修复 bug
- `docs:` - 文档更新
- `style:` - 代码格式（不影响功能）
- `refactor:` - 重构
- `test:` - 测试
- `chore:` - 构建/工具变更

**常用命令：**
```bash
git status          # 查看状态
git log --oneline   # 查看提交历史
git diff            # 查看修改
git stash           # 暂存修改
git stash pop       # 恢复修改
git branch -a       # 查看所有分支
git checkout <branch>  # 切换分支
```

---

### 了解什么设计模式（MVC，观察者模式对应 Qt 信号槽）

**答：**

**1. MVC 模式（Model-View-Controller）**

**在 Qt 中的应用：**
- `QListView` + `QStringListModel` = View + Model
- `QTreeView` + `QStandardItemModel` = View + Model
- Controller 通常是业务逻辑层

**相册项目中的应用：**
- `ProTreeWidget` (View) + `ProTreeItem` (Model) 
- `PicShow` (View) + 图片数据 (Model)

**2. 观察者模式（Observer Pattern）**

**Qt 中的实现：信号槽机制**
```cpp
// 被观察者（Subject）
class ProTreeWidget : public QTreeWidget {
    Q_OBJECT
signals:
    void SigUpdateSelected(const QString& path); // 通知观察者
};

// 观察者（Observer）
class PicShow : public QDialog {
    Q_OBJECT
public slots:
    void SlotSelectItem(const QString& path) { // 响应通知
        // 更新图片显示
    }
};

// 连接（注册观察者）
connect(pro_tree_widget, &ProTreeWidget::SigUpdateSelected,
        pic_show, &PicShow::SlotSelectItem);
```

**3. 单例模式（Singleton）**

```cpp
// Qt 中常用的应用程序级单例
QApplication *app = QApplication::instance(); // 获取单例
```

**4. 工厂模式（Factory Pattern）**

```cpp
// 根据类型创建不同的对象
QTreeWidgetItem* createItem(int type) {
    switch(type) {
        case TreeItemPro: return new ProTreeItem(...);
        case TreeItemDir: return new ProTreeItem(...);
        case TreeItemPic: return new ProTreeItem(...);
    }
}
```

**5. 策略模式（Strategy Pattern）**

```cpp
// Qt 中的排序策略
QTreeWidget::setSortingEnabled(true);
treeWidget->header()->setSortIndicator(0, Qt::AscendingOrder);
```

**6. 代理模式（Proxy Pattern）**

```cpp
// Qt 中的排序代理
QSortFilterProxyModel *proxyModel = new QSortFilterProxyModel(this);
proxyModel->setSourceModel(&model);
proxyModel->setFilterCaseSensitivity(Qt::CaseInsensitive);
view->setModel(proxyModel);
```

---

### 问你：有什么想问我的？

**答：**

**建议问的问题（展示你的思考和兴趣）：**

1. **关于团队：**
   - "请问团队目前的技术栈是什么？除了 Qt 还会用到哪些技术？"
   - "团队的代码审查流程是怎样的？"

2. **关于项目：**
   - "这个相册项目未来的迭代计划是什么？会加入哪些新功能？"
   - "项目中有哪些技术难点需要解决？"

3. **关于成长：**
   - "实习生入职后会有导师带领吗？会参与哪些项目？"
   - "团队对实习生的技术成长有什么期望？"

4. **关于技术：**
   - "团队在跨平台开发方面有什么经验？会用到哪些平台特定的技术？"
   - "项目中有使用到哪些设计模式？"

**不要问的问题：**
- 薪资待遇（HR 面再问）
- 加班情况（显得不积极）
- 太基础的问题（说明没准备）

**示例回答：**
"我想了解一下，如果我有幸加入团队，主要会参与哪些项目？团队目前在技术上有哪些挑战需要解决？这能帮助我更好地准备入职后的工作。"
