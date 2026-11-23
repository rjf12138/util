# util 库打包说明（pm 包）

## 打包

```bash
./pack.sh        # 编译 debug/release 两套 libutil.a，发布到 ~/.project_manager/repo/
./pack.sh -f     # 同名同版本已存在时强制覆盖
```

产物：

- `~/.project_manager/repo/util-0.1.2.pm`
- `~/.project_manager/repo/util-0.1.2.pm.sha256`

包内结构：

```text
util/
├── manifest.json        # link_libs=[util]，依赖 basic/data_structure/protocol/system
├── include/util.h       # 安装到 extern_inc/util/
└── lib/{debug,release}/libutil.a
```

## 使用

```bash
pm deps add util -p MyApp
```

```cpp
#include <util/util.h>
```

## 测试

```bash
pm deps install util
pm build util
```

- `Test_MsgBus`：gtest 用例，整套约 22 秒，已实测通过；
- `Test_Timer`：交互式测试，运行后输入 `q` 退出。

## 升级版本

同步修改 `.pmconfig/config.json`、`pack/manifest.json`、`pack/pack.sh` 中的版本号，然后执行 `./pack.sh`。
