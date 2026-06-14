use axum::{
    body::Body,
    http::{Request, StatusCode},
    middleware::Next,
    response::Response,
};
use std::time::Instant;

/// 请求计时中间件
/// 记录请求处理时间，并通过 tracing 输出
pub async fn request_timer(req: Request<Body>, next: Next) -> Response {
    let start = Instant::now();
    let method = req.method().clone();
    let uri = req.uri().clone();

    // 继续处理请求
    let response = next.run(req).await;

    let elapsed = start.elapsed();
    let status = response.status();

    tracing::info!(
        "{} {} -> {} ({}ms)",
        method,
        uri,
        status,
        elapsed.as_millis(),
    );

    response
}

/// 简单鉴权中间件
/// 检查 Authorization: Bearer my-token
/// 不通过则返回 401 UNAUTHORIZED
pub async fn require_auth(req: Request<Body>, next: Next) -> Result<Response, StatusCode> {
    let auth_header = req
        .headers()
        .get("Authorization")
        .and_then(|v| v.to_str().ok());

    match auth_header {
        Some("Bearer my-token") => Ok(next.run(req).await),
        _ => Err(StatusCode::UNAUTHORIZED),
    }
}