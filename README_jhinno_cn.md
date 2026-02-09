# jhinno 模块 for pdsh

## 概述

jhinno 模块将 pdsh 与 jhinno 作业调度器集成，允许您在由 jhinno 管理的节点上执行命令。该模块通过两种命令支持查询节点：

- `jjobs` - 通过作业ID查询节点信息（数字作业标识符），返回作业分配的执行节点
- `jhosts` - 查询节点和节点组信息，返回属于节点组的节点或集群中的所有节点

## 功能特性

- **按作业ID查询**：使用数字作业ID获取分配给特定作业的执行节点（通过jjobs）
- **按节点组查询**：使用字母数字组标识符获取属于特定节点组的节点（通过jhosts）
- **查询所有节点**：获取集群中所有正常节点（特殊值：`all`）
- **过滤UNKNOWN节点**：默认自动跳过状态为 `UNKNOWN` 的节点
- **包含UNKNOWN节点**：选项可包含状态为UNKNOWN的节点
- **环境变量支持**：设置 `JOBS_JOBID` 来指定默认作业ID或节点组

## 安装

### 使用 jhinno 支持构建

要构建带有 jhinno 模块支持的 pdsh：

```bash
./configure --with-jhinno
make
make install
```

### 要求

jhinno 模块需要以下命令在您的 PATH中可用：

- `jjobs` - 用于查询作业分配的命令
- `jhosts` - 用于查询节点状态的命令

## 使用方法

### 基础用法

#### 1. 按数字作业ID查询

获取分配给特定作业的节点：

```bash
pdsh -j 64882 hostname
```

这将在作业ID 64882 分配的所有节点上执行 `hostname` 命令。

#### 2. 按节点组查询

获取属于特定节点组的节点：

```bash
pdsh -j c9A75 hostname
```

#### 3. 查询所有正常节点

获取所有状态为正常的节点（过滤掉UNKNOWN节点）：

```bash
pdsh -j all hostname
```

#### 4. 包含UNKNOWN节点

在查询中包含状态为UNKNOWN的节点：

```bash
pdsh -j all --jhinno-include-unknown hostname
```

#### 5. 使用环境变量

设置 `JOBS_JOBID` 环境变量来指定默认作业ID：

```bash
export JOBS_JOBID=64882
pdsh hostname
```

### 高级用法

#### 多个查询

查询多个作业或节点组：

```bash
pdsh -j 64882,64883,64884 hostname
```

或混合作业ID和节点组：

```bash
pdsh -j 64882,c9A75,c9A76 hostname
```

**重要**：当查询多个作业或混合使用jjobs和jhosts来源时，模块会自动处理重复的节点。例如，如果一个节点同时出现在作业ID查询和节点组查询中，它只会被执行一次。最终节点列表在合并所有来源后会进行去重。

#### 命令执行

在节点上执行命令：

```bash
# 检查系统负载
pdsh -j all "uptime"

# 运行多个命令
pdsh -j all "df -h && free -m"

# 检查磁盘空间
pdsh -j all "du -sh /var/log"

# 列出正在运行的进程
pdsh -j all "ps aux | grep myapp"
```

#### 静默模式

抑制输出中的主机名前缀：

```bash
pdsh -j all -q "hostname"
```

#### 限制输出

检查节点是否响应：

```bash
pdsh -j all -n
```

## 节点状态过滤

在使用 `-j all` 或节点组查询时，jhinno 模块会自动过滤掉状态为 `UNKNOWN` 的节点。这是默认行为。

### UNKNOWN节点示例

默认情况下会过滤掉状态如下的节点：

```
hpc-gpu-node06 UNKNOWN  UNKNOWN      -        -        -       -     -       -     - -
```

### 过滤选项

1. **默认**：过滤掉UNKNOWN节点
   ```bash
   pdsh -j all hostname
   ```

2. **包含UNKNOWN**：包含所有节点，不管状态如何
   ```bash
   pdsh -j all --jhinno-include-unknown hostname
   ```

## 模块冲突

jhinno 模块与以下模块冲突，因为它们使用相同的 `-j` 选项：

- `slurm` 模块
- `torque` 模块

**重要**：不要同时启用jhinno、slurm和torque模块。

## 内部实现

### 作业ID检测

模块自动检测作业标识符的类型：

- **数字**（如 `64882`）→ 使用 `jjobs` 命令
- **字母数字**（如 `c9A75`）→ 使用 `jhosts` 命令
- **特殊值 `all`** → 使用 `jhosts` 命令获取所有节点

### 命令执行

对于数字作业ID，模块执行：
```bash
jjobs -o exec_host:4096 <jobid>
```

输出格式：
```
EXEC_HOST
64*ev-hpc-compute098:64*ev-hpc-compute164:64*ev-hpc-compute171
```

模块从这种格式中提取节点名称，忽略数字前缀（CPU数量）。

对于节点组或所有节点，模块执行：
```bash
jhosts attrib -w <jobgroup>
```

输出格式：
```
HOST_NAME    type     model    ncpus   maxmem  maxswap nsocket ncore nthread ngpus nnodes RESOURCES
ev-hpc-test02 LINUX64  AMD64      128  385816M       0K       2    64       1     -      2 -
```

模块提取第一列（HOST_NAME），并过滤掉第二列（type）为 `UNKNOWN` 的行。

## 故障排除

### 模块未找到

如果收到关于jhinno模块未找到的错误：

```bash
# 检查模块是否已加载
pdsh -L | grep jhinno

# 使用jhinno支持重新构建
./configure --with-jhinno
make
make install
```

### 命令未找到

如果收到关于 `jjobs` 或 `jhosts` 命令未找到的错误：

1. 确保已安装jhinno作业调度器
2. 验证命令在您的PATH中：
   ```bash
   which jjobs jhosts
   ```
3. 使用正确的路径配置模块

### 未找到节点

如果pdsh报告未找到节点：

1. 验证作业ID是否有效：
   ```bash
   jjobs | grep <jobid>
   ```

2. 检查节点是否为UNKNOWN状态：
   ```bash
   jhosts attrib -w | grep UNKNOWN
   ```

3. 尝试包含UNKNOWN节点：
   ```bash
   pdsh -j all --jhinno-include-unknown -n
   ```

### 权限被拒绝

如果收到权限被拒绝错误：

1. 检查您的身份验证方法（rsh、ssh等）
2. 确保您在目标节点上有适当的权限
3. 如果使用SSH，验证SSH密钥已正确配置

## 示例

### 示例1：检查系统信息

```bash
# 从所有节点获取uname
pdsh -j all "uname -a"

# 检查内核版本
pdsh -j all "uname -r"

# 检查发行版
pdsh -j all "cat /etc/os-release | grep PRETTY_NAME"
```

### 示例2：系统维护

```bash
# 检查所有节点的磁盘空间
pdsh -j all "df -h"

# 检查内存使用情况
pdsh -j all "free -h"

# 检查正在运行的服务
pdsh -j all "systemctl list-units --type=service --state=running"

# 检查系统日志
pdsh -j all "tail -20 /var/log/syslog"
```

### 示例3：应用程序部署

```bash
# 复制文件到作业节点
pdcp -j 64882 /local/myapp /opt/

# 安装软件包
pdsh -j 64882 "yum install -y myapp"

# 重启服务
pdsh -j 64882 "systemctl restart myapp"
```

### 示例4：监控

```bash
# 检查CPU负载
pdsh -j all "uptime"

# 检查网络连接
pdsh -j all "netstat -an | grep ESTABLISHED | wc -l"

# 检查进程数
pdsh -j all "ps aux | wc -l"
```

## 性能考虑

- 该模块为每个查询执行外部命令（`jjobs` 或 `jhosts`）
- 对于大型集群（>1000节点），考虑缓存结果或限制节点数量
- 根据您的网络容量调整并发度（`-f` 选项）：
  ```bash
  pdsh -j all -f 64 "hostname"
  ```

## 安全考虑

- 确保配置了适当的身份验证（SSH密钥、rhosts等）
- 在使用提升权限运行命令时要谨慎
- 在生产系统上运行之前审查输出
- 在整个集群上运行之前，在节点的子集上测试

## 贡献

要为jhinno模块贡献改进：

1. Fork pdsh仓库
2. 进行您的更改
3. 彻底测试
4. 提交pull request

## 许可证

jhinno模块是pdsh的一部分，根据GNU通用公共许可证（GPL）授权。

## 支持

对于问题、疑问或贡献：

- GitHub: https://github.com/YUTIAN0/pdsh
- 邮件列表: pdsh-users@lists.sourceforge.net

## 版本历史

- **1.0** - 初始版本
  - 支持jjobs和jhosts命令
  - 作业ID和节点组查询
  - 自动UNKNOWN节点过滤
  - 环境变量支持
