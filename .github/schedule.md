## Schedule
| 时间 | 任务安排 |
| :------: | :------: |
| 2017/09/02-2017/09/15 | 完成station动态调整 |
| 2017/09/15-2017/09/30 | 完成station数目的计算 |
| 2017/10/08-2017/10/15 | 制定补充实验计划 |
| 2017/10/15-2017/10/20 | 做实验 |
| 2017/10/20-2017/10/31 | 修改文章 |

之前的安排：
- [x] ToDo: 实现ElasticActor的论文实验，使之支持
  - dispatcher.c: 位置无关的移动
  - actor station： scheduler的实现，使之支持actor数目的动态调节，即论文中的数学模型
  - build step: cd KVStore/app/Driver `$ make` will get *.so and *.c files, execute `$ moloader -F config/all.conf`, if you get the error msg that `No route to host` it means the zookeeper server has been down.
  - 实现思路：
    - 看懂KVStore例子的含义，跑通一个简单的例子，弄清分布式数据的传递和处理流程
    - 弄清楚mola的相关接口含义
    - 再考虑如何实现dispather和scheduler
- [x] ToDo: two things, collect useful info to compute Ni-opt to determine the num of actor stations & granularity adjustment of scheduler
  - KVStore要实现这样一种功能见moloader.cpp源文件，该源文件是整个应用的entrance
  - 主要实现两个功能，一个是实时收集各类信息，定期地计算station的数目，另外一个就是scheduler，scheduler的调度单元是actor station，目前的版本也是基于此
  - scheduler以actor station为调度单位，actor迁移的策略。先弄清楚station和actor的关系？TODO查阅station的定义源码，从4个station扩展到8个station期间需要做哪些事情？
  - scheduler轮询计算的策略：每隔一段时间计算station的数目，根据给出的opt公式计算，首先先统计公式所需要的各个参数。e.g. K表示actor的种类，其值需要解析all.conf去统计；actor的到达率，在scheduler中设置时间窗口去统计...
  - all.conf 添加新的配置参数表征轮询计算的时间间隔，所以需要对应修改moloader的解析方法
  - 着手实现的时候先新建一个branch再开始写
  - context上下文是指actor的状态，此次实现不需要考虑actor的状态
  - 看懂dispatcher的实现思路，暂时`不考虑Scatter-Gather模型`，对应dispatch group

拖了很久了，争取国庆节之前完成论文的实验代码，有点突破性的进展吧：
- 重新研读一遍ElasticActor的代码
- 看懂KVStore的基本实现原理
- 看懂mola框架的接口
- 整个工程的调试（分布式的可能不太好调试，就用log）
