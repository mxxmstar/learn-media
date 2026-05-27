use media_ai_log::{LogConfig, init_logger, log_info, log_error, log_debug, log_warn};

fn main() {
    // 创建日志配置
    let config = LogConfig {
        level: "debug".to_string(),
        json_format: false,  // 设为 true 查看 JSON 输出
        with_thread_id: true,
        with_target: true,
        with_ansi: true,
        ..Default::default()
    };
    
    // 初始化日志系统
    init_logger(&config).expect("Failed to initialize logger");
    
    println!("日志系统已初始化，开始测试日志输出...");
    
    // 测试不同级别的日志
    log_info!("应用程序启动");
    log_debug!("调试信息");
    log_info!("用户登录成功");
    log_warn!("警告：磁盘空间不足");
    log_error!("错误：数据库连接失败");
    
    println!("日志测试完成");
}