# 第一课：从一个按钮到应用主窗口

状态：待动手。本课不转码、不链接 FFmpeg；先构建后续界面的基础。

## 运行后的样子

一个标题为 QtFFmpegStudio 的窗口，里面有说明文字和“开始学习 Qt”按钮。点击按钮后，底部状态栏显示“按钮点击成功，下一步学习文件选择”。

你已在 QtHelloWorld 中用过 QApplication、QPushButton 和 connect。本课增加 QMainWindow、central widget 和布局管理。

## 第一步：确认工具链

先按 [README 的构建说明](../../README.md#构建) 配置环境。在 CLion 中使用 Qt 配套的 MinGW 13.1，并设置 `-DCMAKE_PREFIX_PATH=D:/QT/6.8.3/mingw_64`。不要复制 QtHelloWorld 的构建缓存。

## 第二步：创建 CMakeLists.txt

在本项目根目录创建这个文件，内容如下。此处是待你创建的示例，当前目录还没有可构建的应用。

```cmake
cmake_minimum_required(VERSION 3.27)
project(QtFFmpegStudio LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets)

add_executable(QtFFmpegStudio src/main.cpp)
target_link_libraries(QtFFmpegStudio PRIVATE Qt6::Widgets)
```

`Qt6::Widgets` 会传递所需的头文件路径和依赖。AUTOMOC 是为后续含 Q_OBJECT 的自定义类准备的；本课没有自定义信号，不需要在 main.cpp 中添加 Q_OBJECT。

## 第三步：创建 src/main.cpp

```cpp
#include <QApplication>
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QWidget>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    QMainWindow window;
    window.setWindowTitle(QStringLiteral("QtFFmpegStudio"));
    window.resize(800, 500);

    auto* central = new QWidget(&window);
    auto* layout = new QVBoxLayout(central);
    auto* title = new QLabel(QStringLiteral("我的 FFmpeg 图形化转码工具"), central);
    auto* button = new QPushButton(QStringLiteral("开始学习 Qt"), central);

    layout->addWidget(title);
    layout->addWidget(button);
    layout->addStretch();
    window.setCentralWidget(central);
    window.statusBar()->showMessage(QStringLiteral("准备就绪"));

    QObject::connect(button, &QPushButton::clicked, &window, [&window]() {
        window.statusBar()->showMessage(
            QStringLiteral("按钮点击成功，下一步学习文件选择"));
    });

    window.show();
    return app.exec();
}
```

保存为 UTF-8。先看懂这四处：

1. QApplication 管理 GUI 应用，app.exec() 进入事件循环。
2. QMainWindow 提供主窗口结构；central 承载内容，布局放在 central 上。
3. 布局负责安排控件；addStretch() 把多余空间放在下方。
4. connect 把 clicked 信号连接到 lambda；`&window` 作为连接上下文，其销毁时连接也随之失效。

central、控件与布局形成 Qt 对象所有权关系；关闭窗口并退出 main 后，window 析构会释放其拥有的子对象，不需要手动逐个 delete。更复杂的跨线程对象生命周期后续单独学习。

## 第四步：构建和运行

CLion 加载 CMake 后，构建并运行 QtFFmpegStudio。运行环境的 PATH 包含 Qt 与 MinGW 的 bin，见环境文档。

如果想使用终端，可在项目根目录的 PowerShell 中执行以下命令。它们针对上述两个文件已创建的状态；本轮没有执行这些构建命令。

```powershell
$env:PATH = 'D:\QT\6.8.3\mingw_64\bin;D:\QT\Tools\mingw1310_64\bin;' + $env:PATH
& 'D:\CLion 2023.3.6\bin\cmake\win\x64\bin\cmake.exe' -S . -B build -G Ninja '-DCMAKE_MAKE_PROGRAM=D:/CLion 2023.3.6/bin/ninja/win/x64/ninja.exe' '-DCMAKE_CXX_COMPILER=D:/QT/Tools/mingw1310_64/bin/g++.exe' '-DCMAKE_PREFIX_PATH=D:/QT/6.8.3/mingw_64' '-DCMAKE_BUILD_TYPE=Debug'
& 'D:\CLion 2023.3.6\bin\cmake\win\x64\bin\cmake.exe' --build build
& '.\build\QtFFmpegStudio.exe'
```

每条命令成功后再执行下一条。若本机 CLion 路径有变化，以设置页面显示的 CMake/Ninja 实际路径为准。

## 第五步：验收与练习

- [ ] 窗口出现，标题和中文文字正确。
- [ ] 点击按钮，状态栏出现预期文字。
- [ ] 改变窗口大小，控件仍排列正常。
- [ ] 连续点击后仍响应，关闭后进程退出。
- [ ] 自己将按钮文字改成“选择视频（下一课实现）”，能找到改动位置。
- [ ] 能解释：为什么布局不直接设在 QMainWindow 上？为什么这次不需要自己 delete button？

把结果记入自己的学习笔记。下一课拆出 MainWindow 类，加入文件路径输入框和 QFileDialog；第一课不要先加入后台线程。

查阅：[Qt Widgets](https://doc.qt.io/qt-6.8/qtwidgets-index.html)、[信号与槽](https://doc.qt.io/qt-6.8/signalsandslots.html)。
