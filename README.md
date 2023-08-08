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

- `bogus-control-flow`: 虚假控制流
- `flattening`: 控制流平坦化
- `mba-substitute`: 多项式 MBA 指令替换
- `string-obfus`: 字符串加密
- `indirect-call`: 间接调用混淆

```bash
opt --load-pass-plugin="<your/dll/path>" --passes='...;...;...' 
```

目前 NewPassManager 似乎不能通过`-Xclang -fpass-plugin=... -mllvm ...`来传参整合到工具链中，如果需要指定 Pass 只能通过 `opt` 或者拆分成更小的 Dll 使用