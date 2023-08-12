# BronyaObfus

在 Windows 下整合了 [Pluto-Obfuscator](https://github.com/bluesadi/Pluto-Obfuscator) 和 [Arkari](https://github.com/KomiMoe/Arkari) 的部分混淆

## 编译

修改 CMakeLists.txt 第9行和第10行的目录为本地对应的 LLVM 项目目录

```bash
mkdir build
cd build
cmake ..
```

然后用 Visual Studio 打开`.sln`项目，根据原 LLVM 项目的编译选项选择 Debug/Release 生成项目，根据需要修改 `PassRegistry.cpp`

## 使用

- `bogus-control-flow`: 虚假控制流
- `flattening`: 控制流平坦化
- `mba-substitute`: 多项式 MBA 指令替换
- `string-obfus`: 字符串加密
- `indirect-call`: 间接调用混淆

Visual Studio2022 下载[LLVM2022](https://github.com/KomiMoe/llvm2019/releases)

在项目属性中选择 **LLVM**

![](https://bronya-1256118329.cos.ap-shanghai.myqcloud.com/img/20230804000036.png "VS2022-fig0")

然后在新增的 LLVM 中将`Use lld-link`关闭

![](https://bronya-1256118329.cos.ap-shanghai.myqcloud.com/img/20230803235958.png "VS2022-fig1")

关闭**优化**，关闭 C/C++ 命令行中的**从父级或项目默认设计继承**，如果使用 Plugin 来加载 Pass ，`clang-cl.exe`` 的目录设置为编译 Plugin 的 LLVM 目录

```bash
opt --load-pass-plugin="<your/dll/path>" --passes='...;...;...' 
```

或者

```bash
clang++ -Xclang -fpass-plugin="<your/dll/path1>" -Xclang -fpass-plugin="<your/dll/path2>"
```

目前 NewPassManager 似乎不能通过`-Xclang -fpass-plugin=... -mllvm ...`来传参整合到工具链中，如果需要指定 Pass 只能通过 `opt` 或者拆分成更小的 Dll 使用