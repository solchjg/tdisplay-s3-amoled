FROM python:3.12-slim
ENV TZ=America/New_York PYTHONUNBUFFERED=1
WORKDIR /app
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt
COPY water_monitor/ water_monitor/
# /data holds water.toml, the archive and the exports.
WORKDIR /data
ENTRYPOINT ["python", "-m", "water_monitor", "-c", "/data/water.toml"]
CMD ["poll", "--every", "900"]
