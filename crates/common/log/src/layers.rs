use tracing_subscriber:: {
    fmt,
    Registry,
    Layer,
    filter,
};
use tracing_appender::rolling::{RollingFileAppender, Rotation};
use tracing_appender::non_blocking::WorkerGuard;
use std::path::Path;

use crate::config::LogConfig;
/// 控制台日志输出层
pub struct ConsoleLayer {
    pub layer: Box<dyn tracing_subscriber::Layer<Registry> + Send + Sync>,
}

impl ConsoleLayer {
    /// 构建控制台日志输出层
    /// # 参数
    /// * `config` - 日志配置
    /// # 返回值
    /// * `Self` - 控制台日志输出层
    pub fn new(config: &LogConfig) -> Self {        
        let layer = if config.json_format {
            Box::new(
                fmt::layer()
                    .with_thread_ids(config.with_thread_id)
                    .with_thread_names(config.with_thread_name)
                    .with_target(config.with_target)
                    .with_file(config.with_file_line)
                    .with_line_number(config.with_file_line)
                    .with_ansi(config.with_ansi)
                    .json()
            ) as Box<dyn tracing_subscriber::Layer<Registry> + Send + Sync>
        } else {
            Box::new(
                fmt::layer()
                    .with_thread_ids(config.with_thread_id)
                    .with_thread_names(config.with_thread_name)
                    .with_target(config.with_target)
                    .with_file(config.with_file_line)
                    .with_line_number(config.with_file_line)
                    .with_ansi(config.with_ansi)
            ) as Box<dyn tracing_subscriber::Layer<Registry> + Send + Sync>
        };
        
        Self { layer }
    }
}

/// 日志文件层配置
pub struct FileLayerConfig {
    pub log_dir: String,    // 日志目录
    pub file_prefix: String,    // 日志文件前缀
    pub rotation: Rotation,    // 日志文件滚动策略
    pub json_format: bool,    // 是否使用 JSON 格式输出日志
    pub with_thread_id: bool,    // 是否显示线程ID
    pub with_thread_name: bool,    // 是否显示线程名称
    pub with_target: bool,    // 是否显示目标模块信息
    pub with_file_line: bool,    // 是否显示文件名和行号
}

impl From<&LogConfig> for FileLayerConfig {
    fn from(config: &LogConfig) -> Self {
        let log_file = config.log_file.as_deref().unwrap_or("logs/app.log");
        let path = Path::new(log_file);
        
        let log_dir = path.parent()
            .and_then(|p| p.to_str())    // 转换为字符串 or None
            .unwrap_or("logs")    // 如果 None 则使用默认值 "logs"
            .to_string();
            
        let file_prefix = path.file_stem()  // file_stem() 返回文件名，不包含扩展名
            .and_then(|s| s.to_str())    // 转换为字符串 or None
            .unwrap_or("app")    // 如果 None 则使用默认值 "app"
            .to_string();

        let rotation = match config.rotation.as_deref() {
            Some("hourly") => Rotation::HOURLY,
            Some("daily") => Rotation::DAILY,
            Some("minutely") => Rotation::MINUTELY,
            _ => Rotation::NEVER,
        };

        Self {
            log_dir,
            file_prefix,
            rotation,
            json_format: config.json_format,
            with_thread_id: config.with_thread_id,
            with_thread_name: config.with_thread_name,
            with_target: config.with_target,
            with_file_line: config.with_file_line,
        }
    }
}

/// 日志文件层
pub struct FileLayer { 
    pub guard: WorkerGuard,    // 日志文件层线程守卫
    pub layer: Box<dyn tracing_subscriber::Layer<Registry> + Send + Sync>,    // 日志文件层
}

impl FileLayer {
    /// 构建日志文件层
    /// # 参数
    /// * `config` - 日志文件层配置
    /// # 返回值
    /// * `anyhow::Result<Self>` - 日志文件层结果
    pub fn new(config: &FileLayerConfig) -> anyhow::Result<Self> {
        // 确保日志目录存在，否则创建
        std::fs::create_dir_all(&config.log_dir)?;

        // 创建日志文件滚动追加器
        let appender = RollingFileAppender::new(
            config.rotation.clone(),
            &config.log_dir,
            &format!("{}.log", config.file_prefix),        
        );

        // 创建非阻塞日志文件追加器
        let (non_blocking, file_guard) = tracing_appender::non_blocking(appender);

        // 创建日志文件层
        let layer: Box<dyn tracing_subscriber::Layer<Registry> + Send + Sync> = if config.json_format {
            Box::new(
                fmt::layer()
                    .with_writer(non_blocking)
                    .with_thread_ids(config.with_thread_id)
                    .with_target(config.with_target)
                    .with_file(config.with_file_line)
                    .with_line_number(config.with_file_line)
                    .json()
            ) as Box<dyn tracing_subscriber::Layer<Registry> + Send + Sync>
        } else {
            Box::new(
                fmt::layer()
                    .with_writer(non_blocking)
                    .with_thread_names(config.with_thread_name)
                    .with_target(config.with_target)
                    .with_file(config.with_file_line)
                    .with_line_number(config.with_file_line)
            ) as Box<dyn tracing_subscriber::Layer<Registry> + Send + Sync>
        };
        Ok(Self {
            guard: file_guard,
            layer,
        })
    }
}

/// 错误日志层（单独记录错误）
pub struct ErrorLayer {
    pub guard: WorkerGuard,
    pub layer: Box<dyn tracing_subscriber::Layer<Registry> + Send + Sync>,
}

impl ErrorLayer {
    /// 创建只记录 error 级别的日志层
    /// # 参数
    /// * `config` - 日志文件层配置
    /// # 返回值
    /// * `anyhow::Result<Self>` - 错误日志层结果
    pub fn new(config: &FileLayerConfig) -> anyhow::Result<Self> {
        // 确保日志目录存在
        std::fs::create_dir_all(&config.log_dir)?;

        // 创建错误日志文件追加器
        let appender = RollingFileAppender::new(
            config.rotation.clone(),
            &config.log_dir,
            &format!("{}_error.log", config.file_prefix),
        );

        // 创建非阻塞写入器
        let (non_blocking, file_guard) = tracing_appender::non_blocking(appender);

        // 构建只记录错误的日志层
        let layer = Box::new(
            fmt::layer()
                .with_writer(non_blocking)
                .with_thread_ids(config.with_thread_id)
                .with_thread_names(config.with_thread_name)
                .with_target(config.with_target)
                .with_file(config.with_file_line)
                .with_line_number(config.with_file_line)
                .json()
                .with_filter(filter::LevelFilter::ERROR)
        ) as Box<dyn tracing_subscriber::Layer<Registry> + Send + Sync>;

        Ok(Self {
            guard: file_guard,
            layer,
        })
    }
}

/// 多层组合构建器
pub struct LayerBuilder {
    console_enabled: bool,
    file_enabled: bool,
    error_file_enabled: bool,
    config: LogConfig,
}

impl LayerBuilder {
    pub fn new(config: LogConfig) -> Self {
        Self {
            console_enabled: true,
            file_enabled: config.log_file.is_some(),
            error_file_enabled: config.log_file.is_some(),
            config,
        }
    }

    /// 禁用控制台输出
    pub fn without_console(mut self) -> Self {
        self.console_enabled = false;
        self
    }

    /// 禁用文件输出
    pub fn without_file(mut self) -> Self {
        self.file_enabled = false;
        self.error_file_enabled = false;
        self
    }

    /// 禁用错误日志文件
    pub fn without_error_file(mut self) -> Self {
        self.error_file_enabled = false;
        self
    }

    /// 构建所有层
    /// 顺序为：控制台、文件、错误日志文件
    /// # 返回值
    /// * `anyhow::Result<(Vec<Box<dyn tracing_subscriber::Layer<Registry> + Send + Sync>>, Vec<WorkerGuard>)>` - 所有层和线程守卫
    pub fn build(self) -> anyhow::Result<(Vec<Box<dyn tracing_subscriber::Layer<Registry> + Send + Sync>>, Vec<WorkerGuard>)> {        
        // dyn 表示这是一个trait 对象（动态分发）
        // + Send + Sync 表示这个trait 对象的实现者可以安全地在多个线程之间传递
        let mut layers: Vec<Box<dyn tracing_subscriber::Layer<Registry> + Send + Sync>> = Vec::new();
        let mut guards: Vec<WorkerGuard> = Vec::new();

        // 添加控制台层
        if self.console_enabled {
            let console_layer = ConsoleLayer::new(&self.config);
            layers.push(Box::new(console_layer.layer));
        }

        // 添加文件层
        if self.file_enabled {
            let file_config = FileLayerConfig::from(&self.config);
            let file_layer = FileLayer::new(&file_config)?;
            guards.push(file_layer.guard);
            layers.push(Box::new(file_layer.layer));
        }

        // 添加错误日志层
        if self.error_file_enabled {
            let file_config = FileLayerConfig::from(&self.config);
            let error_layer = ErrorLayer::new(&file_config)?;
            guards.push(error_layer.guard);
            layers.push(Box::new(error_layer.layer));
        }

        Ok((layers, guards))
    }
}


#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_console_layer_creation() {
        let config = LogConfig::default();
        let _layer = ConsoleLayer::new(&config);
        // 只要能创建成功就说明基本功能正常
    }

    #[test]
    fn test_file_layer_config_from_log_config() {
        let config = LogConfig {
            log_file: Some("/var/logs/myapp/app.log".to_string()),
            rotation: Some("daily".to_string()),
            ..Default::default()
        };

        let file_config = FileLayerConfig::from(&config);
        assert_eq!(file_config.log_dir, "/var/logs/myapp");
        assert_eq!(file_config.file_prefix, "app");
        assert_eq!(file_config.rotation, Rotation::DAILY);
    }

    #[test]
    fn test_file_layer_config_default_path() {
        let config = LogConfig::default();
        let file_config = FileLayerConfig::from(&config);
        assert_eq!(file_config.log_dir, "logs");
        assert_eq!(file_config.file_prefix, "app");
    }

    #[test]
    fn test_rotation_parsing() {
        let test_cases = vec![
            ("hourly", Rotation::HOURLY),
            ("daily", Rotation::DAILY),
            ("minutely", Rotation::MINUTELY),
            ("never", Rotation::NEVER),
            ("invalid", Rotation::NEVER),
        ];

        for (input, expected) in test_cases {
            let config = LogConfig {
                rotation: Some(input.to_string()),
                log_file: Some("logs/test.log".to_string()),
                ..Default::default()
            };
            let file_config = FileLayerConfig::from(&config);
            assert_eq!(file_config.rotation, expected, "Failed for input: {}", input);
        }
    }

    #[test]
    fn test_layer_builder_default() {
        let config = LogConfig::default();
        let builder = LayerBuilder::new(config);
        assert!(builder.console_enabled);
        assert!(!builder.file_enabled); // 默认没有 log_file
    }

    #[test]
    fn test_layer_builder_with_file() {
        let config = LogConfig {
            log_file: Some("logs/test.log".to_string()),
            ..Default::default()
        };
        let builder = LayerBuilder::new(config);
        assert!(builder.console_enabled);
        assert!(builder.file_enabled);
        assert!(builder.error_file_enabled);
    }

    #[test]
    fn test_layer_builder_without_console() {
        let config = LogConfig::default();
        let builder = LayerBuilder::new(config).without_console();
        assert!(!builder.console_enabled);
    }

    #[test]
    fn test_layer_builder_without_file() {
        let config = LogConfig {
            log_file: Some("logs/test.log".to_string()),
            ..Default::default()
        };
        let builder = LayerBuilder::new(config).without_file();
        assert!(!builder.file_enabled);
        assert!(!builder.error_file_enabled);
    }
}