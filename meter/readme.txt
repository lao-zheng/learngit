
git 123456789




统一命名问题：
    water_meter                 水表 
    electric_meter              电表
    heat_meter                  热表
    photovoltaic_inverter       光伏逆变器
    charging_pile               充电桩

------------------------------------------------------------------------------------------
充电桩电表
    /userdata/data/app/collect/charging_pile/charging_pile
    /userdata/data/app/collect/charging_pile/charging_pile_config.toml
    /userdata/data/app/collect/charging_pile/log/charging_pile_log.log

    /app/charging_pile
    /app/charging_pile_config.toml
    /app/log/charging_pile_log.log

充电桩：0.0.0.0:5001
    读取全部表用电量    /api/collect/v1/chargingPile/totalKWH/all
    读取单个表用电量    /api/collect/v1/chargingPile/totalKWH/{id}
    示例测试            http://0.0.0.0:5004/api/collect/v1/chargingPile/totalKWH/all

------------------------------------------------------------------------------------------

------------------------------------------------------------------------------------------
电表：
物理存储地址：
    /userdata/data/app/collect/electric_meter/electric_meter
    /userdata/data/app/collect/electric_meter/electric_meter_config.toml
    /userdata/data/app/collect/electric_meter/log/electric_meter_log.log

容器内映射
    /app/electric_meter
    /app/electric_meter_config.toml
    /app/log/electric_meter_log.log


电表：0.0.0.0:5002
    /api/collect/v1/electricMeter/totalKWH/all                          读取全部表用电量
    /api/collect/v1/electricMeter/totalKWH/{id}                         读取单个表用电量    
    http://0.0.0.0:5001/api/collect/v1/electricMeter/totalKWH/all       示例测试 


------------------------------------------------------------------------------------------

热表
物理存储地址：
    /userdata/data/app/collect/heat_meter/heat_meter
    /userdata/data/app/collect/heat_meter/heat_meter_config.toml
    /userdata/data/app/collect/heat_meter/log/heat_meter_log.log
容器内映射：（程序中实际目录）
   /app/heat_meter
   /app/heat_meter_config.toml
   /app/log/heat_meter_log.log

热表:0.0.0.0:5003
    /api/collect/v1/heatMeter/totalKWH/all                          读取全部表供热量 
    http://0.0.0.0:5003/api/collect/v1/heatMeter/totalKWH/all       示例测试 

------------------------------------------------------------------------------------------
光伏逆变器
物理存储地址：
    /userdata/data/app/collect/photovoltaic_inverter/photovoltaic_inverter
    /userdata/data/app/collect/photovoltaic_inverter/photovoltaic_inverter_config.toml
    /userdata/data/app/collect/photovoltaic_inverter/log/photovoltaic_inverter_log.log

容器
    /app/photovoltaic_inverter
    /app/photovoltaic_inverter_config.toml
    /app/log/photovoltaic_inverter_log.log

光伏:0.0.0.0:5004 
    /api/collect/v1/photovoltaicInverter/totalKWH/all                       读取全部表发电量   
    /api/collect/v1/photovoltaicInverter/totalKWH/{id}                      读取单个表发电量 
    http://0.0.0.0:5004/api/collect/v1/photovoltaicInverter/totalKWH/all    示例测试  

------------------------------------------------------------------------------------------

------------------------------------------------------------------------------------------
水表：
物理存储地址：
    /userdata/data/app/collect/water_meter/water_meter
    /userdata/data/app/collect/water_meter/water_meter_config.toml
    /userdata/data/app/collect/water_meter/log/water_meter_log.log

容器内映射：（程序中实际目录）
   /app/water_meter
   /app/water_meter_config.toml
   /app/log/water_meter_logs.log

水表:0.0.0.0:5005
    /api/collect/v1/waterMeter/totalT/all                       读取全部表用水量
    /api/collect/v1/waterMeter/totalT/{id}                      读取单个表用水量   
    http://0.0.0.0:5002/api/collect/v1/waterMeter/totalT/all    示例测试 

------------------------------------------------------------------------------------------


水表示例报文：正常
{
    "message": [12345.67, 2.01, -1, 401.00, 50.00, 2.00, 2.00, 2.00, 2.07],
    "result": 0,
    "timestamp": 1758938560
}

水表示例报文：错误
{
    "message":"error",      错误原因
    "result": 1,
    "timestamp": 1758938560
}
------------------------------------------------------------------------------------------

编译器：
sudo apt install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu
------------------------------------------------------------------------------------------
读取流程：
    打开串/网口->读取数据->http读取
重试流程
    打开串/网口->读取数据->其中一个表超时->赋值-1，跳过->http读取
    打开串/网口->读取数据->全部超时->重试x次-->重新打开串/网口 -> 重试x次 -> 重新打开x次-> 退出程序 -> 容器监控重新启动程序
------------------------------------------------------------------------------------------


数据除-1外，统一保留两位小数
外部可传入配置文件路径，也可以不传，使用默认路径
数据读取使用缓存数据，不使用实时读取

------------------------------------------------------------------------------------------





