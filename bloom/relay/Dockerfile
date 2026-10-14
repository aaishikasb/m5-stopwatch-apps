FROM python:3.12-slim
WORKDIR /app
ENV PYTHONUNBUFFERED=1 PYTHONDONTWRITEBYTECODE=1 PORT=8080
COPY server.py .
# /data must be a persistent volume, mounted by the hosting platform.
CMD ["sh", "-c", "exec python server.py --bind 0.0.0.0 --port \"${PORT:-8080}\" --database /data/bloom.sqlite3 --behind-https-proxy"]
