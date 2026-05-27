use tracing_subscriber::{
    EnvFilter,
    Registry,
    layer::SubscriberExt,
    layer::Identity,
    util::SubscriberInitExt,
    Layer,
};
use crate::config::LogConfig;
use crate::layers::LayerBuilder;

pub fn init_logger(config: &LogConfig) -> anyhow::Result<()> {
    let env_filter = EnvFilter::try_from_default_env()
        .unwrap_or_else(|_| EnvFilter::new(&config.level));

    let builder = LayerBuilder::new(config.clone());
    let (layers, _guards) = builder.build()?;

    // 将所有动态层通过 and_then 组合成单个层，再与 env_filter 组合
    let combined_layer = layers.into_iter().fold(
        Box::new(Identity::new()) as Box<dyn Layer<Registry> + Send + Sync>,
        |combined, layer| Box::new(combined.and_then(layer))
    );

    let subscriber = Registry::default()
        .with(combined_layer)
        .with(env_filter);

    subscriber.try_init()?;
    Ok(())
}

pub fn init_logger_default() -> anyhow::Result<()> {
    let config = LogConfig::default();
    init_logger(&config)
}