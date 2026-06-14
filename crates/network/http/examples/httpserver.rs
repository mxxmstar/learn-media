/// HTTP 服务器示例
/// 使用 cartes_network_http 库构建并启动服务器
use cartes_network_http::{build_router, new_store};

#[tokio::main]
async fn main() {
    tracing_subscriber::fmt().init();

    let store = new_store();
    let app = build_router(store);

    let listener = tokio::net::TcpListener::bind("127.0.0.1:3000")
        .await
        .unwrap();

    println!("🚀 Server running at http://127.0.0.1:3000");
    println!("   GET  /");
    println!("   GET  /hello/{{name}}");
    println!("   GET  /json");
    println!("   GET  /users");
    println!("   POST /users");
    println!("   GET  /users/:id");

    axum::serve(listener, app)
        .await
        .unwrap();
}