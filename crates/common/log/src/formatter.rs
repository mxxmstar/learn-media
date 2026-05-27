use tracing_subscriber::fmt::{format, FormatEvent, FormatFields};
use time::OffsetDateTime;

/// 自定义日志格式器
pub struct CustomFormatter;

impl<S, N> FormatEvent<S, N> for CustomFormatter
where
    // S: tracing_subscriber::registry::LookupSpan<'a> + 'a,
    // N: for<'writer> FormatFields<'writer> + 'static,
    S: tracing::Subscriber + for<'a> tracing_subscriber::registry::LookupSpan<'a>,
    N: for<'a> FormatFields<'a> + 'static,
{
    fn format_event(
        &self, 
        ctx: &tracing_subscriber::fmt::FmtContext<'_, S, N>,
        mut writer: format::Writer<'_>,
        event: &tracing::Event<'_>,
    ) -> std::fmt::Result {
        // 时间戳
        // 时间戳
        let timestamp = OffsetDateTime::now_utc();
        let format_description = time::format_description::parse("[year]-[month]-[day] [hour]:[minute]:[second].[subsecond digits:3]")
            .map_err(|_| std::fmt::Error)?;
        let time_str = timestamp.format(&format_description).map_err(|_| std::fmt::Error)?;
        write!(writer, "{}", time_str)?;

        // 日志级别
        let level = event.metadata().level();
        write!(writer, "[{}]", level)?;

        // 线程ID
        let thread_id = std::thread::current().id();
        write!(writer, "[{:?}]", thread_id)?;

        // 目标模块信息
        if event.metadata().target() != "" {
            write!(writer, "{}: ", event.metadata().target())?;
        }

        // 日志消息
        ctx.field_format().format_fields(writer.by_ref(), event)?;  // 借用 writer 按照格式化器的格式写入日志消息
        writeln!(writer)
    }
}


// cargo test -p media-ai-log formatter::tests::test_formatter_compiles
#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_formatter_compiles() {
        // 这个测试只是为了确保 formatter 能够编译
        let _formatter = CustomFormatter;
    }
}