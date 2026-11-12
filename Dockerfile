FROM node:24-alpine AS frontend
WORKDIR /web
COPY web/package*.json ./
RUN npm ci
COPY web/ ./
RUN npm run build

FROM python:3.13-slim AS runtime
ENV PYTHONDONTWRITEBYTECODE=1 PYTHONUNBUFFERED=1 DATABASE_PATH=/data/pascal.sqlite STATIC_DIR=/app/web/dist
WORKDIR /app
COPY server/requirements.txt /app/server/requirements.txt
RUN pip install --no-cache-dir -r server/requirements.txt && useradd --uid 10001 --create-home pascal && mkdir /data && chown pascal:pascal /data
COPY server/ /app/server/
COPY --from=frontend /web/dist /app/web/dist
USER pascal
EXPOSE 8000
HEALTHCHECK --interval=15s --timeout=5s --start-period=10s --retries=3 CMD python -c "import urllib.request; urllib.request.urlopen('http://127.0.0.1:8000/api/health', timeout=3)"
CMD ["uvicorn", "server.app:create_app", "--factory", "--host", "0.0.0.0", "--port", "8000", "--no-proxy-headers"]
