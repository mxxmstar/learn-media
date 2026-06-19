pub mod handlers;
pub mod middleware;
pub mod users;

use axum::{
    middleware as axum_mw,
    routing::{get, post},
    Router,
};
use tower_http::cors::CorsLayer;
use users::UserStore;
use std::sync::Arc;
use std::sync::Mutex;

/// 构建完整的路由
pub fn build_router(store: UserStore) -> Router {
    // ---- API 路由组（公开） ----
    let api_routes = Router::new()
        .route("/users", get(handlers::get_users).post(handlers::post_user))
        .route("/users/:id", get(handlers::get_user_by_id));

    // ---- Admin 路由组（需认证） ----
    let admin_routes = Router::new()
        .route("/admin/stats", get(handlers::admin_stats))
        .layer(axum_mw::from_fn(middleware::require_auth));

    // ---- 合并为完整路由 ----
    Router::new()
        // 公开路由
        .route("/", get(handlers::hello))
        .route("/hello/:name", get(handlers::hello_name))
        .route("/json", get(handlers::json_demo))
        // 嵌套 API 路由，变成 /api/xxx
        .nest("/api", api_routes)
        .nest("/api", admin_routes)
        // 全局注入状态
        .with_state(store)
        // 全局中间件,对所有路由生效
        .layer(CorsLayer::permissive())
        .layer(axum_mw::from_fn(middleware::request_timer))
}

/// 创建一个初始化的 UserStore
pub fn new_store() -> UserStore {
    Arc::new(Mutex::new(Vec::new()))
}