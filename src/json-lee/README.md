## Task
 two things:
 - collect useful info to compute opt num of actor stations
 - granularity adjustment of scheduler
   - BackUp: CentOS关闭防火墙，[link](http://www.centoscn.com/CentosBug/osbug/2016/0702/7562.html)
   - remember to close of firewall of servers by `$ service iptables stop & /etc/init.d/iptables status`
   - compare-and-swap(CAS) operation, lock-free method, see [here](http://www.cnblogs.com/Mainz/p/3546347.html)