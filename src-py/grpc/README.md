# Python gRPC frame receiver

This is a minimal receiver for frames sent by the C++ gRPC demo.

Install dependencies:

```powershell
python -m pip install -r src-py\grpc\requirements.txt
```

Start the Python server:

```powershell
python src-py\grpc\frame_server.py --host 127.0.0.1 --port 18080 --save-dir src-py\grpc\received_frames
```

Build the C++ sender:

```powershell
.\script\build-src-cpp.ps1 -Target grpc -Config Debug
```

Generate protobuf files manually:

```powershell
powershell -ExecutionPolicy Bypass -File script\generate-grpc-proto.ps1
```

Or generate each side separately:

```powershell
powershell -ExecutionPolicy Bypass -File src-cpp\modules\grpc\script\generate-proto.ps1
powershell -ExecutionPolicy Bypass -File src-py\grpc\script\generate-proto.ps1
```

Run the C++ sender:

```powershell
src-cpp\modules\grpc\build\grpc_frame_sender_demo.exe --server 127.0.0.1:18080 --fps 1 --count 5
```

Run the Python sender:

```powershell
python src-py\grpc\frame_client.py --server 127.0.0.1:18080 --fps 1 --count 5
```

You can also send a real encoded frame:

```powershell
src-cpp\modules\grpc\build\grpc_frame_sender_demo.exe --image D:\path\to\frame.jpg --format jpeg --count 5
```
