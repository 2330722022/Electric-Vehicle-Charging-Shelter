package com.example.onenet215;

import android.content.Context;
import android.util.Log;
import android.widget.Toast;

import org.eclipse.paho.client.mqttv3.IMqttDeliveryToken;
import org.eclipse.paho.client.mqttv3.MqttCallback;
import org.eclipse.paho.client.mqttv3.MqttClient;
import org.eclipse.paho.client.mqttv3.MqttConnectOptions;
import org.eclipse.paho.client.mqttv3.MqttException;
import org.eclipse.paho.client.mqttv3.MqttMessage;
import org.eclipse.paho.client.mqttv3.persist.MemoryPersistence;

import java.nio.charset.StandardCharsets;
import java.security.cert.X509Certificate;
import java.util.Base64;

import javax.net.ssl.SSLContext;
import javax.net.ssl.SSLSocketFactory;
import javax.net.ssl.TrustManager;
import javax.net.ssl.X509TrustManager;

public class MqttManager {
    private static final String TAG = "MqttManager";

    private static final String MQTT_BROKER_TCP = "tcp://mqtts.heclouds.com:1883";
    private static final String MQTT_BROKER_SSL = "ssl://mqtts.heclouds.com:8883";
    private static final String MQTT_BROKER_WS = "ws://mqtts.heclouds.com:8083/mqtt";

    private static final String MQTT_BROKER = MQTT_BROKER_SSL;

    private static final String USER_ID = "509781";
    private static final String ACCESS_KEY = "3ffd453bafa04ffbb8775ee13eead513";

    private static final String PRODUCT_ID = "67k36rzgOO";
    private static final String DEVICE_NAME = "test1";

    private static final String DEVICE_KEY = "3ffd453bafa04ffbb8775ee13eead513";

    private static final String FIXED_TOKEN = "d7oSlSRNi5c4V7RXtBZfLA==;expiry=1830297599";

    private String deviceToken;

    private static final String SUBSCRIBE_TOPIC = "$sys/" + PRODUCT_ID + "/" + DEVICE_NAME + "/thing/property/post";
    private static final String PUBLISH_TOPIC = "$sys/" + PRODUCT_ID + "/" + DEVICE_NAME + "/thing/property/set";
    private static final String EVENT_TOPIC = "$sys/" + PRODUCT_ID + "/" + DEVICE_NAME + "/thing/event/+/post";
    private static final String WILDCARD_TOPIC = "$sys/" + PRODUCT_ID + "/" + DEVICE_NAME + "/thing/#";

    private static final int MAX_RECONNECT_ATTEMPTS = 10;
    private static final long BASE_RECONNECT_DELAY_MS = 3000;

    private MqttClient mqttClient;
    private MqttConnectOptions connectOptions;
    private Context context;
    private OnMessageReceivedListener messageListener;
    private OnConnectionStatusChangeListener connectionStatusListener;

    private boolean isConnected = false;
    private long lastReceiveTime = 0;
    private int reconnectAttempts = 0;

    public interface OnMessageReceivedListener {
        void onMessageReceived(String topic, String payload);
    }

    public interface OnConnectionStatusChangeListener {
        void onConnectionChanged(boolean connected);
    }

    public MqttManager(Context context) {
        this.context = context;
        initMqtt();
    }

    private void initMqtt() {
        try {
            String clientId = PRODUCT_ID + "&" + DEVICE_NAME;
            Log.d(TAG, "初始化MQTT - ClientId: " + clientId);

            generateDeviceToken();

            mqttClient = new MqttClient(MQTT_BROKER, clientId, new MemoryPersistence());

            connectOptions = new MqttConnectOptions();
            connectOptions.setUserName(clientId);

            if (deviceToken != null && !deviceToken.isEmpty()) {
                connectOptions.setPassword(deviceToken.toCharArray());
            } else {
                Log.e(TAG, "设备级Token生成失败");
                return;
            }

            connectOptions.setConnectionTimeout(20);
            connectOptions.setKeepAliveInterval(60);
            connectOptions.setAutomaticReconnect(true);
            connectOptions.setCleanSession(true);
            connectOptions.setMqttVersion(MqttConnectOptions.MQTT_VERSION_3_1_1);

            if (MQTT_BROKER.startsWith("ssl://")) {
                connectOptions.setSocketFactory(createSSLSocketFactory());
            }

            mqttClient.setCallback(new MqttCallback() {
                @Override
                public void connectionLost(Throwable cause) {
                    Log.e(TAG, "MQTT连接丢失: " + (cause != null ? cause.getMessage() : "未知"));
                    isConnected = false;
                    if (connectionStatusListener != null) {
                        connectionStatusListener.onConnectionChanged(false);
                    }
                    tryReconnect();
                }

                @Override
                public void messageArrived(String topic, MqttMessage message) {
                    String payload = new String(message.getPayload(), StandardCharsets.UTF_8);
                    Log.d(TAG, "收到MQTT消息 - Topic: " + topic);
                    lastReceiveTime = System.currentTimeMillis();
                    reconnectAttempts = 0;

                    if (messageListener != null) {
                        messageListener.onMessageReceived(topic, payload);
                    }
                }

                @Override
                public void deliveryComplete(IMqttDeliveryToken token) {
                    Log.d(TAG, "消息发送完成 - MessageId: " + token.getMessageId());
                }
            });

        } catch (MqttException e) {
            Log.e(TAG, "初始化MQTT失败", e);
        }
    }

    private void generateDeviceToken() {
        if (FIXED_TOKEN != null && !FIXED_TOKEN.isEmpty() && !FIXED_TOKEN.trim().isEmpty()) {
            deviceToken = FIXED_TOKEN;
            return;
        }

        try {
            String version = "2018-10-31";
            String resourceName = "products/" + PRODUCT_ID + "/devices/" + DEVICE_NAME;
            long expiry = System.currentTimeMillis() / 1000 + 3600 * 24 * 30;
            String method = "md5";

            String signStr = expiry + "\n" + method + "\n" + resourceName + "\n" + version;

            javax.crypto.Mac mac = javax.crypto.Mac.getInstance("HmacMD5");
            mac.init(new javax.crypto.spec.SecretKeySpec(ACCESS_KEY.getBytes(StandardCharsets.UTF_8), "HmacMD5"));
            byte[] signBytes = mac.doFinal(signStr.getBytes(StandardCharsets.UTF_8));

            String signature = Base64.getEncoder().encodeToString(signBytes);

            StringBuilder tokenBuilder = new StringBuilder();
            tokenBuilder.append("version=").append(version)
                       .append("&res=").append(java.net.URLEncoder.encode(resourceName, "UTF-8"))
                       .append("&et=").append(expiry)
                       .append("&method=").append(method)
                       .append("&sign=").append(java.net.URLEncoder.encode(signature, "UTF-8"));

            deviceToken = tokenBuilder.toString();

        } catch (Exception e) {
            Log.e(TAG, "动态生成Token失败", e);
        }
    }

    private SSLSocketFactory createSSLSocketFactory() {
        try {
            SSLContext sslContext = SSLContext.getInstance("TLS");
            sslContext.init(null, new TrustManager[]{new X509TrustManager() {
                @Override
                public void checkClientTrusted(X509Certificate[] chain, String authType) {}

                @Override
                public void checkServerTrusted(X509Certificate[] chain, String authType) {}

                @Override
                public X509Certificate[] getAcceptedIssuers() {
                    return new X509Certificate[0];
                }
            }}, null);
            return sslContext.getSocketFactory();
        } catch (Exception e) {
            Log.e(TAG, "创建SSL Socket工厂失败", e);
            return null;
        }
    }

    private void tryReconnect() {
        if (reconnectAttempts >= MAX_RECONNECT_ATTEMPTS) {
            Log.w(TAG, "重连次数已达上限(" + MAX_RECONNECT_ATTEMPTS + ")，停止自动重连");
            return;
        }

        reconnectAttempts++;
        long delay = BASE_RECONNECT_DELAY_MS * (long) Math.pow(2, Math.min(reconnectAttempts - 1, 4));
        Log.d(TAG, "第" + reconnectAttempts + "/" + MAX_RECONNECT_ATTEMPTS + "次尝试重连，等待" + (delay / 1000) + "秒...");

        new Thread(() -> {
            try {
                Thread.sleep(delay);
                if (!isConnected && mqttClient != null) {
                    connect();
                }
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
            }
        }).start();
    }

    public void connect() {
        new Thread(() -> {
            try {
                if (mqttClient == null) {
                    Log.e(TAG, "MQTT客户端未初始化");
                    return;
                }

                if (!mqttClient.isConnected()) {
                    Log.d(TAG, "正在连接MQTT服务器...");

                    long startTime = System.currentTimeMillis();
                    mqttClient.connect(connectOptions);
                    long connectTime = System.currentTimeMillis() - startTime;

                    isConnected = true;
                    lastReceiveTime = System.currentTimeMillis();
                    reconnectAttempts = 0;

                    Log.d(TAG, "MQTT连接成功 - 耗时: " + connectTime + "ms");

                    if (connectionStatusListener != null) {
                        connectionStatusListener.onConnectionChanged(true);
                    }

                    subscribe();
                }
            } catch (MqttException e) {
                Log.e(TAG, "MQTT连接失败 (第" + reconnectAttempts + "次) - 错误码: "
                        + e.getReasonCode() + ", " + e.getMessage());
                isConnected = false;

                if (connectionStatusListener != null) {
                    connectionStatusListener.onConnectionChanged(false);
                }

                tryReconnect();
            }
        }).start();
    }

    private void subscribe() {
        new Thread(() -> {
            try {
                if (mqttClient.isConnected()) {
                    String[] topics = {SUBSCRIBE_TOPIC, EVENT_TOPIC, WILDCARD_TOPIC};
                    int[] qos = {1, 1, 1};
                    mqttClient.subscribe(topics, qos);
                    Log.d(TAG, "订阅主题成功");
                }
            } catch (MqttException e) {
                Log.e(TAG, "订阅主题失败", e);
            }
        }).start();
    }

    public void publishCommand(String property, Object value) {
        new Thread(() -> {
            try {
                if (mqttClient.isConnected()) {
                    String valueStr;
                    if (value instanceof Boolean) {
                        valueStr = String.valueOf(value);
                    } else if (value instanceof Integer) {
                        valueStr = String.valueOf(value);
                    } else {
                        valueStr = "\"" + value + "\"";
                    }

                    String payload = "{\"id\":\"" + System.currentTimeMillis() + "\"," +
                            "\"version\":\"1.0\"," +
                            "\"params\":{" +
                            "\"" + property + "\":" + valueStr +
                            "}}";

                    MqttMessage message = new MqttMessage(payload.getBytes(StandardCharsets.UTF_8));
                    message.setQos(1);
                    message.setRetained(false);

                    mqttClient.publish(PUBLISH_TOPIC, message);
                    Log.d(TAG, "发布指令 - " + property + "=" + value);
                } else {
                    Log.e(TAG, "MQTT未连接，无法发布指令");
                }
            } catch (MqttException e) {
                Log.e(TAG, "发布指令失败", e);
            }
        }).start();
    }

    public void disconnect() {
        new Thread(() -> {
            try {
                if (mqttClient != null && mqttClient.isConnected()) {
                    mqttClient.disconnect();
                    isConnected = false;
                    Log.d(TAG, "MQTT已断开");
                    if (connectionStatusListener != null) {
                        connectionStatusListener.onConnectionChanged(false);
                    }
                }
            } catch (MqttException e) {
                Log.e(TAG, "断开MQTT失败", e);
            }
        }).start();
    }

    public void manualReconnect() {
        reconnectAttempts = 0;
        Log.d(TAG, "手动重连 - 重置重试计数器");
        connect();
    }

    public boolean isConnected() {
        return isConnected && mqttClient != null && mqttClient.isConnected();
    }

    public int getReconnectAttempts() {
        return reconnectAttempts;
    }

    public long getLastReceiveTime() {
        return lastReceiveTime;
    }

    public void setOnMessageReceivedListener(OnMessageReceivedListener listener) {
        this.messageListener = listener;
    }

    public void setOnConnectionStatusChangeListener(OnConnectionStatusChangeListener listener) {
        this.connectionStatusListener = listener;
    }
}