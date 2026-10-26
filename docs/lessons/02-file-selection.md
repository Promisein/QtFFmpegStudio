# 第二课：MainWindow 类与文件选择

本课实现已写入源码并构建通过。你可以先运行观察，再按下面的顺序读代码和动手修改。完成后的窗口能选择输入 MP4、选择输出 MP4 的保存位置，并检查路径；此时不会转码或创建输出视频。

## 1. 先运行，知道自己在学什么

1. 在 CLion 打开本项目，若提示 CMake 文件变化，点击 Reload CMake Project。
2. 继续使用第一课跑通的 MinGW Qt 6.8.3 配置，构建并运行 QtFFmpegStudio。
3. 点击“选择输入视频…”，选择你自己的 MP4，观察输入框。
4. 点击“选择输出文件…”，会默认建议输入文件旁边的 `<原文件名>_converted.mp4`。可以改成其他位置。
5. 点击“检查路径”，阅读下面的反馈；先不要寻找开始转码按钮，真实执行在后续课程接入。

保存对话框中的“保存”在这里表示**选择未来输出文件的路径**，不会立即写入视频。默认建议改了文件名，是为了让输入与输出分开。

## 2. 文件为什么要拆开

本课保留你根目录中的 main.cpp，没有再建立 src/main.cpp。

```text
QtFFmpegStudio/
  CMakeLists.txt
  main.cpp                创建应用，创建窗口，进入事件循环
  src/
    mainwindow.h          声明 MainWindow 类和成员
    mainwindow.cpp        创建控件，实现选择文件与路径检查
```

先打开 [main.cpp](../../main.cpp)，现在只剩下这几个关键动作：

```cpp
QApplication app(argc, argv);
MainWindow window;
window.show();
return app.exec();
```

与第一课相比，创建按钮、布局、连接信号等代码移进了 MainWindow。main 负责启动，窗口类负责界面行为，后面加参数控件时就不用一直扩大 main。

## 3. 阅读 mainwindow.h：把 C++ 类知识接上来

打开 [mainwindow.h](../../src/mainwindow.h)。重点看：

```cpp
class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void selectInputFile();
    void selectOutputFile();
    void checkPaths();
    // 后面是输入框、输出框和反馈标签的成员指针。
};
```

- 继承 QMainWindow，让自己的类具备主窗口能力。
- 构造函数在 main 中的 `MainWindow window;` 执行时被调用，负责初始化界面。
- parent 默认为 nullptr，因此它本身是顶层窗口；构造函数通过 `QMainWindow(parent)` 将父对象传给基类。
- Q_OBJECT 让这个类参与 Qt 元对象机制；private slots 声明本课响应按钮的成员函数。后续再深入自定义信号。
- 槽函数本质上仍是成员函数。这次用成员函数接收 clicked，替代第一课中写在 connect 里的 lambda。

头文件里的 `class QLineEdit;` 是 C++ 前向声明：这里只声明指针，不必展开 QLineEdit 的实现；cpp 中调用它的函数时才 include 对应头文件。基类 QMainWindow 则必须在类定义前完整声明，因此头文件 include QMainWindow。

本课有了 Q_OBJECT，现有 CMAKE_AUTOMOC 会生成相应元对象代码。若遇到 vtable、staticMetaObject 等链接错误，优先检查 CMake 是否列入了 mainwindow.h，并重新加载 CMake。

## 4. 阅读构造函数：创建控件与建立连接

打开 [mainwindow.cpp](../../src/mainwindow.cpp)，先只读构造函数。

QLineEdit 是单行输入框。本课用 `setReadOnly(true)` 让它显示选择结果，仍可选中复制，但不能手动编辑，降低初学时的状态复杂度。

```cpp
inputPathEdit_ = new QLineEdit(central);
inputPathEdit_->setReadOnly(true);
inputPathEdit_->setPlaceholderText(QStringLiteral("请选择一个 MP4 视频"));
```

纵向布局安排输入区、输出区、检查按钮与反馈；每个文件路径和它的选择按钮放进一个横向布局。嵌套布局可以组合出界面，无需写死坐标。

```cpp
auto* inputRow = new QHBoxLayout;
inputRow->addWidget(inputPathEdit_);
inputRow->addWidget(inputButton);
layout->addLayout(inputRow);
```

接着看成员函数连接：

```cpp
connect(inputButton, &QPushButton::clicked,
        this, &MainWindow::selectInputFile);
```

按四个参数读：**谁发出信号 → 什么信号 → 谁接收 → 执行哪个函数**。这里 this 就是当前 MainWindow 对象。输入、输出、检查三个按钮分别连接到三个槽函数。

路径输入框还有 textChanged 连接：如果路径变化，就清除“检查通过”的旧结论，提醒重新检查。只读限制的是键盘输入，代码调用 setText 仍可改变内容并发出信号。

## 5. 阅读 selectInputFile：取得路径，处理取消

```cpp
const QString path = QFileDialog::getOpenFileName(
    this,
    QStringLiteral("选择输入视频"),
    inputPathEdit_->text(),
    QStringLiteral("MP4 视频 (*.mp4 *.MP4)"));

if (path.isEmpty()) {
    return;
}
inputPathEdit_->setText(QDir::toNativeSeparators(path));
```

四个参数分别是父窗口、对话框标题、初始位置、文件过滤器。第一次输入为空时使用对话框默认位置，之后可从已有路径继续选择。

QString 是 Qt 的 Unicode 字符串类型。界面中的路径先一直保留为 QString，不急着转成 std::string；真正传给 FFmpeg 时再学习编码转换。QStringLiteral 用来构造代码中的固定 Qt 字符串。

关键是先检查 isEmpty：用户取消返回空字符串，直接 return，旧值就不会被清空。最后的 toNativeSeparators 让路径按系统习惯显示，Windows 一般显示反斜杠。

文件过滤器只是筛选展示，不能证明媒体有效；后面接入后端还需要实际打开、探测流。

## 6. 阅读 selectOutputFile：选择一个尚不存在的文件

输入是打开现有文件，输出是选择将来创建的文件，因此使用保存模式。

```cpp
QFileDialog dialog(this, QStringLiteral("选择输出文件"));
dialog.setAcceptMode(QFileDialog::AcceptSave);
dialog.setFileMode(QFileDialog::AnyFile);
dialog.setNameFilter(QStringLiteral("MP4 视频 (*.mp4 *.MP4)"));
dialog.setDefaultSuffix(QStringLiteral("mp4"));
dialog.selectFile(suggestedPath);
```

这次创建对话框对象，是为了明确设置 defaultSuffix：输入没有扩展名的名字时补全 mp4。selectFile 设置建议路径，exec 等待用户作出选择。文件对话框有自己的事件处理，这与直接在按钮中等待一段耗时转码不同。

如果点取消，保持输出框原值；如果确认，则从 selectedFiles 读取选中路径。输入还未选择时默认建议 home 目录的 output.mp4；已经选择过输出时，重新打开对话框沿用上次选择。

## 7. 阅读 checkPaths：先做基础检查

本课用 QFileInfo 查询文件信息，用 QDir 查询父目录，按以下顺序检查：

1. 输入是否已选择，是否为存在、可读取的普通文件。
2. 输入是否使用 mp4 扩展名，不区分大小写。
3. 输出是否已选择，是否使用 mp4 扩展名。
4. 输出父目录是否存在，输出位置是否为文件而不是目录。
5. 输入输出是否指向同一个规范路径。

canonicalFilePath 用于取得已有文件的规范路径；未创建的输出没有规范路径，因此使用 absoluteFilePath。Windows 下按忽略大小写比较，拦截常见的同文件选择。

检查结果显示在 feedbackLabel_ 中；状态栏给出简短状态。已存在的输出会提示后续转码前需确认覆盖，此处不会打开或改写它。

注意：这还不是完整的转码前检查。媒体能否解码、编码器是否可用、实际写入权限、硬链接等问题，要在后端接入时进一步验证。本课的“通过”只表示基础路径检查通过。

## 8. CMake 为什么也要改

打开 [CMakeLists.txt](../../CMakeLists.txt)：

```cmake
add_executable(QtFFmpegStudio
        main.cpp
        src/mainwindow.h
        src/mainwindow.cpp
)
target_include_directories(QtFFmpegStudio PRIVATE src)
```

把新的 cpp 加入编译，把头文件列入目标以便 AUTOMOC 处理。include 路径使根目录的 main.cpp 可以直接写 `#include "mainwindow.h"`。现有 Qt 路径、链接和 DLL 配置保留。

## 9. 你亲自完成的验收

- [ ] 不选文件直接检查，提示先选输入。
- [ ] 选一个真实 MP4，输出建议文件名正确。
- [ ] 分别取消输入、输出对话框，旧路径不变。
- [ ] 选中文或带空格路径，输入框显示完整。
- [ ] 输出文件名不写后缀，确认后会补全 .mp4。
- [ ] 选输入文件本身作为输出，再检查，提示不能是同一个文件。
- [ ] 选不同输出路径，检查通过；确认并没有生成输出视频。
- [ ] 检查通过后换一个文件，旧结论被清除。
- [ ] 缩放窗口、重复操作、关闭窗口正常。

本轮助手已在 Qt offscreen 与 Windows 平台下使用非原生文件对话框进行了交互验证，并检查 Windows 渲染截图中的中文与布局；你的 CLion 实际运行会使用 Windows 原生对话框，请亲自完成上面的操作。

## 10. 两个小练习

1. 将默认输出名从 `<stem>_converted.mp4` 改为 `<stem>_qt.mp4`，重新运行验证。提示：找到 selectOutputFile 中构造 suggestedPath 的位置。
2. 不运行代码，先解释：如果删掉 selectInputFile 中的 `if (path.isEmpty()) return;`，取消选择会发生什么？之后实际试一次，再恢复保护。

完成后进入第三课：增加旋转、倍速和编码器控件，将控件中的值整理成独立配置。

参考：[QFileDialog](https://doc.qt.io/qt-6.8/qfiledialog.html)、[QMainWindow](https://doc.qt.io/qt-6.8/qmainwindow.html)、[对象树与所有权](https://doc.qt.io/qt-6.8/objecttrees.html)、[QFileInfo](https://doc.qt.io/qt-6.8/qfileinfo.html)。
