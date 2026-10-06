# Models

The board deployment currently uses:

```text
/opt/yolov8n/yolov8n_uint8.nb
```

Keep large generated model files out of this project directory unless there is
a specific reason to version or archive one here.

For local development, copy or symlink the model as needed:

```bash
ln -s /opt/yolov8n/yolov8n_uint8.nb models/yolov8n_uint8.nb
```
