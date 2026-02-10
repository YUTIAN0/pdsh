# PDSH JHINNO 模块

pdsh-jhinno 模块为 pdsh 提供了支持，用于连接由 JHINNO（景行）作业调度系统管理的节点。

该模块使用 jjobs 和 jhosts 命令获取节点信息，并支持多种节点选择方法。

## 功能特性

- 通过作业ID指定节点（仅限运行中的作业）
- 通过节点组指定节点
- 使用 -j all 指定所有正常节点
- 支持 JOBS_JOBID 或 JH_HOSTS 环境变量
- **新增：完整资源请求串支持，包含 select、order、rusage、span 四个段**
- 新增：-F 选项节点筛选功能
- 新增：当同时使用 -j 资源串和 -F 选项时的冲突检测
- 新增：等待作业自动过滤功能

## 使用方法

### 按作业ID使用（基本用法）

指定运行中作业使用的节点：

```bash
pdsh -j 12345 hostname
```

这会查询 jjobs 命令获取作业的 EXEC_HOST。

### 按节点组使用

使用预定义的节点组：

```bash
pdsh -j c9A75 hostname
pdsh -j compute,password hostname
```

可以用逗号指定多个组：

```bash
pdsh -j group1,group2,group3 hostname
```

### 指定所有正常节点

使用特殊值 "all" 获取所有处于正常状态的节点：

```bash
pdsh -j all hostname
```

### 使用 -F 选项筛选节点（v1.2+）

使用 -F 选项通过资源请求表达式筛选节点：

```bash
# 按内存筛选
pdsh -j all -F "select[mem>1000]" hostname

# 按GPU筛选
pdsh -j all -F "select[gpus>0]" "nvidia-smi"

# 按CPU筛选
pdsh -j all -F "select[cpus>=64]" hostname

# 组合多个筛选条件
pdsh -j c9A75 -F "select[mem>1000 && gpus>0]" hostname
```

-F 选项将表达式直接传递给 jhosts -R 进行节点筛选。

筛选表达式语法：

```
-F "select[表达式]"
```

支持的运算符：>、<、>=、<=、==、!=、&&、||

示例：
- `select[mem>1000]` - 内存 > 1000MB
- `select[gpus>0]` - 至少有 1 个 GPU 的节点
- `select[cpus>=64]` - CPU 数 >= 64 的节点
- `select[mem>1000 && gpus>0]` - 内存 > 1000MB 且有 GPU
- `select[mem<5000 || gpus>2]` - 内存 < 5000MB 或有 >2 个 GPU

### 资源请求串（增强功能 - v1.3+）

-j 选项现在支持完整的资源请求串，包含四个段：

**语法：**
```
-j "select[选择字符串] order[排序字符串] rusage[资源使用字符串] span[跨越字符串]"
```

**四个段：**
- **select**：选择节点的标准
- **order**：如何对满足条件的节点排序
- **rusage**：期望的资源消耗
- **span**：并行批处理作业是否可跨越多个节点

**示例：**

1. **仅使用 select：**
   ```bash
   pdsh -j "select[mem>1000]" hostname
   ```

2. **select + order 组合：**
   ```bash
   pdsh -j "select[mem>1000] order[rusage:mem]" hostname
   ```

3. **select + rusage 组合：**
   ```bash
   pdsh -j "select[gpus>0] rusage[gpu=1]" "nvidia-smi"
   ```

4. **完整使用四个段：**
   ```bash
   pdsh -j "select[mem>1000 && gpus>=2] order[rusage:mem] rusage[gpu=2:mem=16G] span[ptile=1]" hostname
   ```

5. **仅使用 order：**
   ```bash
   pdsh -j "order[rusage:mem]" hostname
   ```

6. **仅使用 rusage：**
   ```bash
   pdsh -j "rusage[gpu=1]" hostname
   ```

**向后兼容性：**
```bash
# 旧方式仍然有效
pdsh -j all -F "select[mem>1000]" hostname
pdsh -j c9A75 -F "select[cpus>=64]" hostname

# 新简化方式
pdsh -j "select[mem>1000]" hostname
```

**筛选表达式：**
- 内存：`select[mem>1000]`、`select[mem<1000]`、`select[mem>=1000 && mem<=5000]`
- GPU：`select[gpus>0]`、`select[gpus>=2]`
- CPU：`select[cpus>=64]`
- 多条件：`select[mem>1000 && gpus>0]`

**冲突检测：**
不能同时使用资源串和 -F 选项：
```bash
# 错误
pdsh -j "select[mem>1000]" -F "select[gpus>0]" hostname
```

这将产生错误：
```
pdsh@host: jhinno: Cannot specify resource request string with both -j and -F options
pdsh@host: jhinno: Use either '-j "expression"' OR '-j group -F "expression"', not both
```

**与节点组结合使用：**
```bash
# 来自 c9A75 组的节点 + 筛选
pdsh -j "c9A75,select[mem>1000]" hostname
```

### 环境变量

#### JOBS_JOBID

设置默认的作业ID或节点组：

```bash
export JOBS_JOBID=12345
pdsh hostname  # 自动使用作业 12345

export JOBS_JOBID=all
pdsh hostname  # 使用所有正常节点

export JOBS_JOBID="select[gpus>0] rusage[gpu=1]"
pdsh "nvidia-smi"  # 使用资源请求串
```

#### JH_HOSTS

设置显式的主机/核心数：

```bash
export JH_HOSTS="node1 128 node2 256 node3 64"
pdsh hostname  # 使用这些主机
```

格式："主机名1 核心数 主机名2 核心数 ..."

### 等待作业处理（新增功能）

当目标是一个等待作业（即没有分配执行节点的作业），或者资源筛选结果没有匹配的节点时，
pdsh 默认返回：

```
"no remote hosts specified"
```

然后退出，不执行命令。

检查作业的 EXEC_HOST：

```bash
jjobs -o exec_host:4096 12345
EXEC_HOST
-
```

如果第二行是 "-"，则该作业正在等待资源分配。

如果作业处于等待状态，并且运行：

```bash
pdsh -j 12345 hostname
```

将会看到：

```
pdsh@host: no remote hosts specified
```

这是预期的 - pdsh 识别出没有为该作业分配节点。

### 模块选项

--jhinno-include-unknown

默认情况下，使用 -j all 时会排除 UNKNOWN 状态的节点。使用此选项可以包含它们：

```bash
pdsh -j all --jhinno-include-unknown hostname
```

### 示例

```bash
# 在作业 12345 的节点上运行
pdsh -j 12345 command

# 在 c9A75 组的节点上运行
pdsh -j c9A75 command

# 在所有正常节点上运行
pdsh -j all command

# 在内存 > 1000MB 的节点上运行
pdsh -j "select[mem>1000]" command

# 在有 GPU 的节点上运行
pdsh -j "select[gpus>0]" "nvidia-smi"

# 在 CPU >= 64 且内存 > 1000MB 的节点上运行
pdsh -j "select[cpus>=64 && mem>1000]" command

# 使用环境变量
export JOBS_JOBID=all
pdsh command

# 使用 JH_HOSTS 指定显式主机列表
export JH_HOSTS="node1 128 node2 256"
pdsh command

# 组合节点组 + 筛选
pdsh -j "c9A75,select[mem>1000]" command

# 使用所有四个段的完整资源请求
pdsh -j "select[mem>1000] order[rusage:mem] rusage[gpu=2] span[ptile=1]" command
```

## 版本历史

- **1.3** - 增强 -j 选项，支持完整资源请求串
  - 支持完整的资源请求串：select、order、rusage、span
  - 简化使用方式：pdsh -j "select[mem>1000]"
  - 可组合多个段：select、order、rusage、span
  - 当同时使用 -j 资源串和 -F 选项时进行冲突检测
  - 与 -j all -F "expression" 方式向后兼容

- **1.2** - 节点筛选支持
  - 添加 -F 选项用于节点筛选
  - 将 jhosts 命令从 "attrib -w" 改为 "-w"
  - 添加等待作业的自动过滤
  - 增强 jhosts 输出中的空格处理

- **1.1** - 基本实现
  - 支持 -j 作业ID和节点组
  - 支持 -j all
  - JOBS_JOBID 和 JH_HOSTS 环境变量
  --jhinno-include-unknown 选项

## 系统要求

- pdsh（需要模块支持）
- jjobs 命令（用于作业ID查找）
- jhosts 命令（用于节点组和筛选）
- 访问 JHINNO（景行）作业调度系统

## 文件

- src/modules/jhinno.c - 模块源代码
- README_jhinno_cn.md - 本文件

## 参见

pdsh(1)、jjobs(1)、jhosts(1)、JHINNO 文档
