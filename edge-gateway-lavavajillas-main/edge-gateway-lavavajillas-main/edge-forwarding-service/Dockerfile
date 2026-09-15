FROM python:3.11-slim

WORKDIR /broker

COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

COPY broker/ /broker/

CMD ["python", "globalBroker.py"]