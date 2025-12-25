# 特殊机票预订系统 - UML建模文档

本文档包含特殊机票预订系统的完整UML建模，使用Mermaid语法绘制。

---

## 一、用例图（Use Case Diagram）

```mermaid
graph TB
    %% 参与者定义
    Student[留学生<br/>Student]
    Team[团队用户<br/>Team User<br/>10人以上]
    Normal[普通用户<br/>Normal User]
    Admin[系统管理员<br/>Admin]
    Payment[支付系统<br/>Payment System]
    
    %% 用例定义
    subgraph 系统边界
        UC1[预订机票<br/>Book Ticket]
        UC1_1[预订普通机票<br/>Book Normal Ticket]
        UC1_2[预订留学生国际机票<br/>Book Student Int'l Ticket]
        UC1_3[预订团体机票<br/>Book Group Ticket]
        UC2[改签机票<br/>Change Ticket]
        UC3[退票机票<br/>Refund Ticket]
        UC4[审核留学生证明<br/>Audit Student Certificate]
        UC5[支付订单<br/>Pay Order]
        UC6[提交留学证明<br/>Submit Certificate]
        UC7[提交乘机人信息<br/>Submit Passenger Info]
        UC8[提交所有乘客信息<br/>Submit All Passengers Info]
    end
    
    %% 参与者与用例关联
    Student -.-> UC1_2
    Team -.-> UC1_3
    Normal -.-> UC1_1
    Admin -.-> UC4
    Student -.-> UC5
    Team -.-> UC5
    Normal -.-> UC5
    
    %% 泛化关系 (父用例到子用例)
    UC1_1 -.->|泛化<br/>generalization| UC1
    UC1_2 -.->|泛化<br/>generalization| UC1
    UC1_3 -.->|泛化<br/>generalization| UC1
    
    %% 包含关系 (include)
    UC1_2 -->|«include»| UC6
    UC1_1 -->|«include»| UC7
    UC1_2 -->|«include»| UC7
    UC1_3 -->|«include»| UC7
    UC1_3 -->|«include»| UC8
    
    %% 扩展关系 (extend)
    UC2 -.->|«extend»| UC1
    UC3 -.->|«extend»| UC1
    UC1_2 -.->|«extend»| UC4
    
    %% 与外部系统关联
    UC5 -.-> Payment
    
    style Student fill:#e1f5ff
    style Team fill:#e1f5ff
    style Normal fill:#e1f5ff
    style Admin fill:#ffe1e1
    style Payment fill:#fff4e1
```

**说明：**
- 核心参与者：留学生、团队用户（10人以上）、普通用户
- 辅助参与者：系统管理员、支付系统
- 泛化关系：三种预订方式都是"预订机票"的特化
- 包含关系：预订时必须提交信息，留学生必须提交证明
- 扩展关系：改签和退票是预订成功后的可选操作；留学生预订需要审核

---

## 二、活动图（Activity Diagram）

```mermaid
flowchart TD
    Start([开始]) --> UserType{选择用户类型}
    
    %% 普通用户分支
    UserType -->|普通用户| Normal1[提交乘机人信息<br/>+ 航班信息]
    Normal1 --> Normal2[生成普通订单]
    Normal2 --> Normal3[支付订单]
    Normal3 --> NormalPay{支付是否成功?}
    NormalPay -->|成功| Normal4[出票]
    Normal4 --> End1([结束])
    
    %% 留学生分支
    UserType -->|留学生| Student1[提交乘机人信息<br/>+ 航班信息<br/>+ 留学证明]
    Student1 --> Student2[系统转发证明给管理员]
    Student2 --> Student3{审核结果}
    Student3 -->|审核通过| Student4[生成留学生订单]
    Student4 --> Student5[支付订单]
    Student5 --> StudentPay{支付是否成功?}
    StudentPay -->|成功| Student6[出票<br/>允许超额行李]
    Student6 --> End2([结束])
    Student3 -->|审核驳回| Student7[提示补充材料]
    Student7 --> Student1
    
    %% 团队用户分支
    UserType -->|团队用户| Team1[提交所有乘客信息<br/>含儿童证明<br/>+ 航班信息]
    Team1 --> Team2[生成团体订单]
    Team2 --> Team3[支付部分费用]
    Team3 --> TeamPay{支付是否成功?}
    TeamPay -->|成功| Team4[出票]
    Team4 --> End3([结束])
    
    %% 支付失败分支
    NormalPay -->|失败| PayFail[提示支付失败]
    StudentPay -->|失败| PayFail
    TeamPay -->|失败| PayFail
    PayFail --> End4([结束])
    
    %% 对象流标注
    Normal1 -.->|乘机人信息对象| ObjPass[乘机人信息<br/>姓名、证件号等]
    Student1 -.->|证明材料对象| ObjCert[证明材料<br/>签证/录取通知]
    Normal2 -.->|订单对象| ObjOrder[订单对象<br/>订单号、航班信息]
    Normal3 -.->|支付结果对象| ObjPay[支付结果<br/>成功/失败]
    Normal4 -.->|机票对象| ObjTicket[机票对象<br/>乘机信息、行李额度]
    
    style Start fill:#90EE90
    style End1 fill:#FFB6C1
    style End2 fill:#FFB6C1
    style End3 fill:#FFB6C1
    style End4 fill:#FFB6C1
    style ObjPass fill:#FFFACD
    style ObjCert fill:#FFFACD
    style ObjOrder fill:#FFFACD
    style ObjPay fill:#FFFACD
    style ObjTicket fill:#FFFACD
```

**说明：**
- 控制流：展示了三种用户类型的完整预订流程
- 对象流：标注了流程中产生的关键对象（乘机人信息、证明材料、订单、支付结果、机票）
- 判断节点：审核结果、支付成功与否
- 循环：审核驳回时回退到提交证明环节

---

## 三、类图（Class Diagram）

```mermaid
classDiagram
    %% 乘机人基类
    class Passenger {
        -String name
        -Date birthDate
        -String idType
        -String idNo
        -String issuePlace
        -Date validity
        +getPassengerInfo() String
    }
    
    %% 留学生类（继承）
    class StudentPassenger {
        -Certificate certificate
        -Double luggageLimit
        +submitCertificate() void
    }
    
    %% 团队类
    class Team {
        -String teamNo
        -Set~Passenger~ passengers
        -int teamSize
        +addPassenger(p: Passenger) void
        +getTeamInfo() String
    }
    
    %% 订单基类
    class Order {
        -String orderNo
        -Date bookTime
        -OrderState state
        -Flight flight
        -Double amount
        +createOrder() void
        +payOrder() void
        +cancelOrder() void
    }
    
    %% 留学生订单（继承）
    class StudentOrder {
        -AuditState auditState
        +auditCertificate() void
    }
    
    %% 团体订单（继承）
    class TeamOrder {
        -Double paidRatio
        -Team team
        +payPartial() void
    }
    
    %% 航班类
    class Flight {
        -String flightNo
        -Date departTime
        -String destination
        -int remainingSeats
        +querySeats() int
        +bookSeat() void
    }
    
    %% 系统管理员类
    class Admin {
        -String adminId
        +auditCertificate(c: Certificate) boolean
    }
    
    %% 证明材料类
    class Certificate {
        -String certType
        -String certNo
        -Date validity
    }
    
    %% 枚举类
    class OrderState {
        <<enumeration>>
        PENDING
        PAID
        CANCELLED
        ISSUED
    }
    
    class AuditState {
        <<enumeration>>
        PENDING
        APPROVED
        REJECTED
    }
    
    %% 继承关系
    StudentPassenger --|> Passenger : 继承
    StudentOrder --|> Order : 继承
    TeamOrder --|> Order : 继承
    
    %% 关联关系
    Order "1" -- "1" Flight : 关联
    Team "1" *-- "*" Passenger : 聚合
    
    %% 聚合关系
    TeamOrder o-- Team : 聚合
    StudentOrder o-- Certificate : 聚合
    StudentPassenger o-- Certificate : 聚合
    
    %% 依赖关系
    Admin ..> Certificate : 依赖
    
    %% 枚举使用
    Order --> OrderState : 使用
    StudentOrder --> AuditState : 使用
```

**说明：**
- 继承关系：StudentPassenger继承Passenger；StudentOrder和TeamOrder继承Order
- 关联关系：Order与Flight一对一关联；Team与Passenger一对多关联
- 聚合关系：TeamOrder包含Team；StudentOrder包含Certificate
- 依赖关系：Admin审核依赖Certificate
- 枚举类型：OrderState和AuditState定义订单状态

---

## 四、序列图（Sequence Diagram）

**场景：留学生国际机票预订完整流程**

```mermaid
sequenceDiagram
    participant Student as 留学生<br/>StudentPassenger
    participant System as 系统<br/>System
    participant Admin as 系统管理员<br/>Admin
    participant Payment as 支付系统<br/>PaymentSystem
    participant FlightSys as 航班系统<br/>FlightSystem
    
    autonumber
    
    %% 提交信息阶段
    Student->>+System: submitPassengerInfo(姓名, 证件号, ...)
    Note right of System: 接收乘机人信息
    
    Student->>System: submitCertificate(签证号, 录取通知扫描件)
    Note right of System: 接收留学证明
    
    %% 审核阶段
    System->>+Admin: forwardCertificate(签证号, 录取通知扫描件)
    Note right of Admin: 管理员审核证明材料
    Admin-->>-System: auditResult(审核通过)
    
    %% 查询航班阶段
    System->>+FlightSys: queryFlight(航班号, 日期)
    FlightSys-->>-System: returnFlightInfo(剩余座位:20, 起飞时间:xxx)
    
    %% 创建订单阶段
    System->>System: createStudentOrder(订单号:OD2025xxx, 行李额度:40kg)
    Note right of System: 内部创建留学生订单
    
    System-->>-Student: showOrder(订单号, 金额, 行李额度)
    
    %% 支付阶段
    Student->>+System: payOrder(支付方式:信用卡, 金额:xxxx)
    System->>+Payment: processPayment(订单号, 金额)
    Note right of Payment: 处理支付请求
    Payment-->>-System: paymentSuccess(交易号:TRxxx)
    
    %% 出票阶段
    System->>+FlightSys: bookSeat(订单号, 乘机人信息)
    FlightSys-->>-System: seatBooked(座位号:35A)
    
    System-->>Student: issueTicket(机票号:TKxxx, 座位号:35A, 行李额度:40kg)
    System-->>-Student: returnOrderState(已出票)
    
    Note over Student,FlightSys: 完整的留学生国际机票预订流程
```

**说明：**
- 交互对象：留学生、系统、管理员、支付系统、航班系统
- 消息类型：同步调用（实线箭头）、返回值（虚线箭头）
- 关键步骤：
  1. 提交乘机人信息和留学证明
  2. 管理员审核证明
  3. 查询航班信息
  4. 创建留学生订单（40kg行李额度）
  5. 支付订单
  6. 预订座位并出票

---

## 五、状态图（State Diagram）

**对象：订单（Order）的生命周期**

```mermaid
stateDiagram-v2
    [*] --> 待提交: 创建订单
    
    %% 留学生订单专属审核流程
    待提交 --> 待审核: 提交留学证明<br/>(留学生订单专属)
    待审核 --> 审核通过: 管理员审核通过
    待审核 --> 审核驳回: 管理员审核驳回
    审核驳回 --> 待提交: 补充证明材料
    
    %% 支付流程
    待提交 --> 待支付: 生成订单
    审核通过 --> 待支付: 生成订单
    
    待支付 --> 部分支付: 支付部分费用<br/>(团体订单专属)
    待支付 --> 已支付: 全额支付
    待支付 --> 已取消: 用户取消预订/<br/>超时未支付
    已取消 --> [*]: 订单结束
    
    %% 出票流程
    部分支付 --> 已出票: 航班系统确认出票
    已支付 --> 已出票: 航班系统确认出票
    
    %% 改签流程
    已出票 --> 待改签: 用户申请改签<br/>(约束: 改签次数≤2)
    待改签 --> 改签中: 系统处理改签
    改签中 --> 已改签: 改签完成
    已改签 --> 已出票: 返回出票状态
    改签中 --> 改签失败: 无剩余座位
    改签失败 --> 已出票: 返回原票状态
    
    %% 退票流程
    已出票 --> 待退票: 用户申请退票
    待退票 --> 已退票: 系统处理退票
    已退票 --> [*]: 订单结束
    待退票 --> 退票失败: 超退票时限
    退票失败 --> 已出票: 返回原票状态
    
    %% 状态注释
    note right of 待审核
        留学生订单需要管理员
        审核留学证明材料
    end note
    
    note right of 部分支付
        团体订单可以先支付
        部分费用再出票
    end note
    
    note right of 待改签
        改签次数限制≤2次
        改签失败返回原状态
    end note
    
    note right of 待退票
        超过退票时限则失败
        返回已出票状态
    end note
```

**说明：**
- 初始状态：待提交
- 核心状态转换：
  - 待提交 → 待审核（留学生订单）→ 审核通过/审核驳回
  - 待提交/审核通过 → 待支付 → 已支付/部分支付
  - 已支付/部分支付 → 已出票
  - 已出票 → 待改签 → 改签中 → 已改签/改签失败
  - 已出票 → 待退票 → 已退票/退票失败
- 终态：已取消、已退票
- 特殊约束：
  - 改签次数≤2
  - 团体订单可部分支付
  - 留学生订单需审核

---

## 六、总结

本文档完整描述了特殊机票预订系统的UML建模，包括：

1. **用例图**：展示了系统的功能需求和参与者交互
2. **活动图**：展示了三种用户类型的完整预订流程（控制流和对象流）
3. **类图**：展示了系统的静态结构和类之间的关系
4. **序列图**：展示了留学生国际机票预订的完整交互过程
5. **状态图**：展示了订单对象的完整生命周期

所有图表均使用Mermaid语法编写，可直接在支持Mermaid的Markdown查看器中渲染显示。

---

## 附录：关键业务规则

1. **留学生特权**：
   - 需提交留学证明（签证/录取通知）
   - 管理员审核通过后方可预订
   - 享有超额行李额度（40kg）

2. **团体订单特点**：
   - 10人以上团队用户
   - 需提交所有乘客信息（含儿童证明）
   - 支持部分支付

3. **订单操作规则**：
   - 改签次数限制≤2次
   - 退票有时限限制
   - 支付超时自动取消

4. **系统参与者**：
   - 核心参与者：留学生、团队用户、普通用户
   - 辅助参与者：系统管理员、支付系统
