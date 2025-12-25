# Unix Project Repository

本仓库包含以下项目：

## 1. 聊天室项目 (Chatroom)

位于 `chatroom/` 目录，是一个基于 C 语言的多线程聊天室服务器和客户端实现。

详细功能说明请查看：[chatroom/功能点说明.md](chatroom/功能点说明.md)

### 快速开始

```bash
cd chatroom
./build.sh
./server     # 在一个终端运行服务器
./client     # 在另一个终端运行客户端
```

## 2. 特殊机票预订系统 - UML建模

位于根目录的 `特殊机票预订系统UML建模.md` 文件。

这是一个完整的 UML 建模文档，包含以下 5 个 Mermaid 图表：

1. **用例图（Use Case Diagram）** - 展示系统功能需求和参与者交互
2. **活动图（Activity Diagram）** - 展示三种用户类型的完整预订流程
3. **类图（Class Diagram）** - 展示系统静态结构和类之间的关系
4. **序列图（Sequence Diagram）** - 展示留学生国际机票预订的完整交互过程
5. **状态图（State Diagram）** - 展示订单对象的完整生命周期

### 查看方式

该文档使用 Mermaid 语法编写，可以通过以下方式查看：

- 在 GitHub 上直接查看（GitHub 原生支持 Mermaid）
- 使用支持 Mermaid 的 Markdown 编辑器（如 VS Code + Mermaid 插件）
- 在线 Mermaid 编辑器：https://mermaid.live/

### 系统特点

- **三种用户类型**：普通用户、留学生、团队用户（10人以上）
- **留学生特权**：需审核留学证明，享有超额行李额度（40kg）
- **团体订单**：支持部分支付，需提交所有乘客信息
- **订单管理**：支持改签（限2次）、退票（有时限）
- **系统参与者**：系统管理员负责审核，对接外部支付系统

---

## 项目结构

```
.
├── chatroom/               # 聊天室项目
│   ├── server.c           # 服务器主程序
│   ├── client.c           # 客户端主程序
│   ├── src/               # 源代码
│   ├── include/           # 头文件
│   ├── tests/             # 测试脚本
│   └── 功能点说明.md      # 功能说明文档
├── 特殊机票预订系统UML建模.md  # UML建模文档
└── README.md              # 本文件
```
