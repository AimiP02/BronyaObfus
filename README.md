# BronyaObfus

在 Windows 下整合了 [Pluto-Obfuscator](https://github.com/bluesadi/Pluto-Obfuscator) 和 [Arkari](https://github.com/KomiMoe/Arkari) 的部分混淆

## 编译

修改 CMakeLists.txt 第9行和第10行的目录为本地对应的 LLVM 项目目录

```bash
mkdir build
cd build
cmake ..
```

然后用 Visual Studio 打开`.sln`项目，选择 Release 生成项目

## 使用

- `-mllvm --bogus-control-flow`: 虚假控制流
- `-mllvm --flattening`: 控制流平坦化
- `-mllvm --mba-substitute`: 多项式 MBA 指令替换
- `-mllvm --string-obfus`: 字符串加密

```bash
clang++ -Xclang -fpass-plugin="<your/dll/path>" -mllvm ... 
```
