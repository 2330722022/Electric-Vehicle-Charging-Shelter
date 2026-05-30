package com.example.onenet215;

import android.content.Context;
import android.content.SharedPreferences;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.util.Iterator;
import java.util.Timer;
import java.util.TimerTask;

/**
 * 数据获取模块（重构版）
 * 功能：
 * 1. 双线程调度：Task_Control（1秒轮询状态）和 Task_Sensor（15秒轮询传感器数据）
 * 2. 指令优先策略：用户操作时暂停传感器轮询，优先发送控制指令
 * 3. 数据占位：请求失败时保持旧数据，显示延迟提示
 * 4. 数据持久化：使用 SharedPreferences 缓存最后一次成功数据
 */
public class DataFetcher {
    private static final String TAG = "DataFetcher";
    
    // 超时设置
    private static final int CONNECT_TIMEOUT = 3000;  // 连接超时：3秒
    private static final int READ_TIMEOUT = 5000;     // 读取超时：5秒
    
    // 轮询间隔
    private static final long CONTROL_INTERVAL = 1000;   // 控制任务：1秒
    private static final long SENSOR_INTERVAL = 15000;   // 传感器任务：15秒
    
    // SharedPreferences 键名
    private static final String PREF_NAME = "DataFetcherCache";
    private static final String KEY_LAST_DATA = "last_successful_data";
    private static final String KEY_LAST_UPDATE_TIME = "last_update_time";
    
    // OneNET API配置（使用固定Token）
    private static final String PRODUCT_ID = "67k36rzgOO";
    private static final String DEVICE_NAME = "test1";
    private static final String QUERY_API_URL = "https://iot-api.heclouds.com/thingmodel/query-device-property?product_id=" + PRODUCT_ID + "&device_name=" + DEVICE_NAME;
    private static final String CONTROL_API_URL = "https://iot-api.heclouds.com/thingmodel/set-device-property";
    private static final String API_TOKEN = "version=2018-10-31&res=products%2F67k36rzgOO%2Fdevices%2Fproject&et=1830297599&method=md5&sign=bt7UPGU6PGqQlnhj6joIzQ%3D%3D";
    
    private final Context context;
    private final SharedPreferences sharedPreferences;
    private final Handler mainHandler;  // 主线程Handler
    private Timer controlTimer;         // 控制任务定时器
    private Timer sensorTimer;          // 传感器任务定时器
    private DataCallback callback;
    private boolean isRunning = false;
    private boolean isSensorPaused = false;  // 传感器轮询是否暂停
    private boolean hasDataDelay = false;    // 是否有数据延迟
    
    public interface DataCallback {
        void onDataReceived(JSONObject data);      // 数据接收成功
        void onError(String error);                 // 错误回调
        void onControlSuccess(String property, Object value);  // 控制成功
        void onDataDelay(boolean hasDelay);         // 数据延迟状态
    }
    
    public DataFetcher(Context context) {
        this.context = context.getApplicationContext();
        this.sharedPreferences = context.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE);
        this.mainHandler = new Handler(Looper.getMainLooper());
    }
    
    /**
     * 设置数据回调
     */
    public void setDataCallback(DataCallback callback) {
        this.callback = callback;
    }
    
    /**
     * 启动双线程轮询器
     */
    public void startPolling() {
        if (isRunning) {
            Log.d(TAG, "轮询器已在运行中");
            return;
        }
        
        isRunning = true;
        isSensorPaused = false;
        hasDataDelay = false;
        
        Log.d(TAG, "启动双线程轮询器 - Control: 1秒, Sensor: 15秒");
        
        // 启动控制任务（每1秒执行）
        controlTimer = new Timer("Task_Control");
        controlTimer.scheduleAtFixedRate(new TimerTask() {
            @Override
            public void run() {
                fetchDeviceStatus();
            }
        }, 0, CONTROL_INTERVAL);
        
        // 启动传感器任务（每15秒执行）
        sensorTimer = new Timer("Task_Sensor");
        sensorTimer.scheduleAtFixedRate(new TimerTask() {
            @Override
            public void run() {
                if (!isSensorPaused) {
                    fetchSensorData();
                }
            }
        }, 0, SENSOR_INTERVAL);
    }
    
    /**
     * 停止轮询器
     */
    public void stopPolling() {
        if (!isRunning) {
            Log.d(TAG, "轮询器未运行");
            return;
        }
        
        isRunning = false;
        
        // 取消所有定时器
        if (controlTimer != null) {
            controlTimer.cancel();
            controlTimer = null;
        }
        if (sensorTimer != null) {
            sensorTimer.cancel();
            sensorTimer = null;
        }
        
        Log.d(TAG, "停止双线程轮询器");
    }
    
    /**
     * 获取缓存的历史数据
     */
    public JSONObject getCachedData() {
        String cachedData = sharedPreferences.getString(KEY_LAST_DATA, null);
        if (cachedData != null) {
            try {
                return new JSONObject(cachedData);
            } catch (JSONException e) {
                Log.e(TAG, "解析缓存数据失败", e);
            }
        }
        return null;
    }
    
    /**
     * 获取最后更新时间
     */
    public long getLastUpdateTime() {
        return sharedPreferences.getLong(KEY_LAST_UPDATE_TIME, 0);
    }
    
    /**
     * 暂停传感器轮询（用于指令优先策略）
     */
    public void pauseSensorPolling() {
        isSensorPaused = true;
        Log.d(TAG, "传感器轮询已暂停");
    }
    
    /**
     * 恢复传感器轮询
     */
    public void resumeSensorPolling() {
        isSensorPaused = false;
        Log.d(TAG, "传感器轮询已恢复");
    }
    
    /**
     * 发送控制指令（优先执行）
     * @param property 属性名称（如 led, beep, temp_threshold）
     * @param value 属性值（Boolean或Integer）
     */
    public void sendControlCommand(String property, Object value) {
        // 暂停传感器轮询
        pauseSensorPolling();
        
        new Thread(() -> {
            HttpURLConnection connection = null;
            try {
                URL url = new URL(CONTROL_API_URL);
                connection = (HttpURLConnection) url.openConnection();
                
                // 设置POST请求
                connection.setRequestMethod("POST");
                connection.setConnectTimeout(CONNECT_TIMEOUT);
                connection.setReadTimeout(READ_TIMEOUT);
                connection.setRequestProperty("Content-Type", "application/json");
                connection.setRequestProperty("authorization", API_TOKEN);
                connection.setDoOutput(true);
                
                // 构建JSON请求体
                JSONObject jsonObject = new JSONObject();
                jsonObject.put("product_id", PRODUCT_ID);
                jsonObject.put("device_name", DEVICE_NAME);
                
                JSONObject params = new JSONObject();
                // 支持Boolean和Integer类型
                if (value instanceof Boolean) {
                    params.put(property, (Boolean) value);
                } else if (value instanceof Integer) {
                    params.put(property, (Integer) value);
                } else {
                    params.put(property, value.toString());
                }
                jsonObject.put("params", params);
                
                String jsonBody = jsonObject.toString();
                Log.d(TAG, "发送控制指令: " + jsonBody);
                
                // 发送请求
                OutputStream os = connection.getOutputStream();
                os.write(jsonBody.getBytes(StandardCharsets.UTF_8));
                os.flush();
                os.close();
                
                int responseCode = connection.getResponseCode();
                
                if (responseCode == HttpURLConnection.HTTP_OK) {
                    BufferedReader reader = new BufferedReader(
                            new InputStreamReader(connection.getInputStream()));
                    StringBuilder response = new StringBuilder();
                    String line;
                    while ((line = reader.readLine()) != null) {
                        response.append(line);
                    }
                    reader.close();
                    
                    Log.d(TAG, "控制指令发送成功: " + response.toString());
                    
                    // 通知UI更新
                    if (callback != null) {
                        mainHandler.post(() -> {
                            callback.onControlSuccess(property, value);
                        });
                    }
                } else {
                    Log.e(TAG, "控制指令发送失败，状态码: " + responseCode);
                    if (callback != null) {
                        mainHandler.post(() -> {
                            callback.onError("控制失败: " + responseCode);
                        });
                    }
                }

            } catch (Exception e) {
                Log.e(TAG, "发送控制指令异常", e);
                if (callback != null) {
                    mainHandler.post(() -> {
                        callback.onError("控制异常: " + e.getMessage());
                    });
                }
            } finally {
                if (connection != null) {
                    connection.disconnect();
                }
                mainHandler.postDelayed(this::resumeSensorPolling, 2000);
            }
        }).start();
    }

    /**
     * 本地直连模式发送控制指令
     * 通过 HTTP GET 向单片机的网关 IP 发送控制指令
     * 格式: http://{gatewayIp}/api/set?type={type}&val={val}
     */
    public void sendLocalControlCommand(String gatewayIp, String type, int val) {
        pauseSensorPolling();
        
        new Thread(() -> {
            HttpURLConnection connection = null;
            try {
                String urlStr = "http://" + gatewayIp + "/api/set?type=" + type + "&val=" + val;
                Log.d(TAG, "本地直连发送指令: " + urlStr);
                
                URL url = new URL(urlStr);
                connection = (HttpURLConnection) url.openConnection();
                connection.setRequestMethod("GET");
                connection.setConnectTimeout(CONNECT_TIMEOUT);
                connection.setReadTimeout(READ_TIMEOUT);
                
                int responseCode = connection.getResponseCode();
                
                if (responseCode == HttpURLConnection.HTTP_OK) {
                    BufferedReader reader = new BufferedReader(
                            new InputStreamReader(connection.getInputStream()));
                    StringBuilder response = new StringBuilder();
                    String line;
                    while ((line = reader.readLine()) != null) {
                        response.append(line);
                    }
                    reader.close();
                    
                    Log.d(TAG, "本地直连指令发送成功: " + response.toString());
                    
                    if (callback != null) {
                        mainHandler.post(() -> {
                            callback.onControlSuccess(type, val);
                        });
                    }
                } else {
                    Log.e(TAG, "本地直连指令发送失败，状态码: " + responseCode);
                    if (callback != null) {
                        mainHandler.post(() -> {
                            callback.onError("本地控制失败: " + responseCode);
                        });
                    }
                }

            } catch (Exception e) {
                Log.e(TAG, "本地直连指令发送异常", e);
                if (callback != null) {
                    mainHandler.post(() -> {
                        callback.onError("本地控制异常: " + e.getMessage());
                    });
                }
            } finally {
                if (connection != null) {
                    connection.disconnect();
                }
                mainHandler.postDelayed(this::resumeSensorPolling, 2000);
            }
        }).start();
    }
    
    /**
     * 获取设备状态（Task_Control - 每1秒）
     */
    private void fetchDeviceStatus() {
        if (!isRunning) {
            return;
        }
        
        // 这里可以查询设备的在线状态、连接状态等
        // 暂时简化为检查是否有新数据
        Log.d(TAG, "Task_Control: 检查设备状态");
    }
    
    /**
     * 获取传感器数据（Task_Sensor - 每15秒）
     */
    private void fetchSensorData() {
        if (!isRunning || isSensorPaused) {
            Log.w(TAG, "Task_Sensor: 传感器轮询被暂停 (isRunning=" + isRunning + ", isSensorPaused=" + isSensorPaused + ")");
            return;
        }
        
        Log.d(TAG, "Task_Sensor: 开始获取传感器数据...");
        new Thread(() -> {
            HttpURLConnection connection = null;
            try {
                URL url = new URL(QUERY_API_URL);
                connection = (HttpURLConnection) url.openConnection();
                
                // 设置请求方法和超时时间
                connection.setRequestMethod("GET");
                connection.setConnectTimeout(CONNECT_TIMEOUT);
                connection.setReadTimeout(READ_TIMEOUT);
                connection.setRequestProperty("authorization", API_TOKEN);
                
                Log.d(TAG, "Task_Sensor: 正在请求: " + QUERY_API_URL);
                int responseCode = connection.getResponseCode();
                Log.d(TAG, "Task_Sensor: 响应码: " + responseCode);
                
                if (responseCode == HttpURLConnection.HTTP_OK) {
                    // 读取响应数据
                    BufferedReader reader = new BufferedReader(
                            new InputStreamReader(connection.getInputStream()));
                    StringBuilder response = new StringBuilder();
                    String line;
                    while ((line = reader.readLine()) != null) {
                        response.append(line);
                    }
                    reader.close();
                    
                    String jsonString = response.toString();
                    Log.d(TAG, "Task_Sensor: HTTP请求成功");
                    
                    // 解析JSON数据
                    JSONObject jsonObject = new JSONObject(jsonString);
                    
                    // 检查errno
                    if (jsonObject.has("errno")) {
                        int errno = jsonObject.optInt("errno", -1);
                        if (errno != 0) {
                            String error = jsonObject.optString("error", "未知错误");
                            Log.e(TAG, "API返回错误: errno=" + errno + ", error=" + error);
                            
                            // 数据延迟标记
                            hasDataDelay = true;
                            if (callback != null) {
                                mainHandler.post(() -> {
                                    callback.onDataDelay(true);
                                    callback.onError("API错误: " + error);
                                });
                            }
                            return;
                        }
                    }
                    
                    // 保存到缓存
                    saveToCache(jsonString);
                    
                    // 清除延迟标记
                    hasDataDelay = false;
                    
                    // 回调通知
                    if (callback != null) {
                        mainHandler.post(() -> {
                            callback.onDataReceived(jsonObject);
                            callback.onDataDelay(false);
                        });
                    }
                } else {
                    // 请求失败，但不打断用户操作，保持缓存数据
                    Log.w(TAG, "Task_Sensor: HTTP请求失败，状态码: " + responseCode);
                    
                    // 数据延迟标记
                    hasDataDelay = true;
                    if (callback != null) {
                        mainHandler.post(() -> {
                            callback.onDataDelay(true);
                            callback.onError("请求失败: " + responseCode);
                        });
                    }
                }

            } catch (java.net.SocketTimeoutException e) {
                Log.w(TAG, "Task_Sensor: HTTP请求超时", e);
                hasDataDelay = true;
                if (callback != null) {
                    mainHandler.post(() -> {
                        callback.onDataDelay(true);
                        callback.onError("网络超时");
                    });
                }
            } catch (Exception e) {
                Log.e(TAG, "Task_Sensor: HTTP请求异常", e);
                hasDataDelay = true;
                if (callback != null) {
                    mainHandler.post(() -> {
                        callback.onDataDelay(true);
                        callback.onError("网络异常: " + e.getMessage());
                    });
                }
            } finally {
                if (connection != null) {
                    connection.disconnect();
                }
            }
        }).start();
    }
    
    /**
     * 保存数据到缓存
     */
    private void saveToCache(String jsonString) {
        SharedPreferences.Editor editor = sharedPreferences.edit();
        editor.putString(KEY_LAST_DATA, jsonString);
        editor.putLong(KEY_LAST_UPDATE_TIME, System.currentTimeMillis());
        editor.apply();
        Log.d(TAG, "数据已保存到缓存");
    }
    
    /**
     * 解析OneNET返回的数据格式
     * 转换为统一的JSONArray格式：[{"identifier":"xxx","value":"xxx"}, ...]
     * 
     * OneNET物模型API返回格式：
     * {
     *   "errno": 0,
     *   "error": "succ",
     *   "data": [
     *     {"identifier": "temperature", "value": "25.5"},
     *     {"identifier": "humidity", "value": "60"}
     *   ]
     * }
     */
    public static JSONArray parseResponse(JSONObject response) throws JSONException {
        JSONArray result = new JSONArray();
        
        // 检查errno
        if (response.has("errno")) {
            int errno = response.optInt("errno", -1);
            if (errno != 0) {
                Log.e(TAG, "API返回错误: errno=" + errno);
                return result;
            }
        }
        
        // 解析data数组
        if (response.has("data")) {
            Object dataObj = response.get("data");
            
            // 如果data是JSONArray，直接处理
            if (dataObj instanceof JSONArray) {
                JSONArray dataArray = (JSONArray) dataObj;
                for (int i = 0; i < dataArray.length(); i++) {
                    JSONObject item = dataArray.getJSONObject(i);
                    String identifier = item.optString("identifier");
                    Object valueObj = item.opt("value");
                    
                    if (identifier != null && !identifier.isEmpty()) {
                        JSONObject dataItem = new JSONObject();
                        dataItem.put("identifier", identifier);
                        dataItem.put("value", valueObj != null ? String.valueOf(valueObj) : "");
                        result.put(dataItem);
                    }
                }
            }
            // 如果data是JSONObject（params格式）
            else if (dataObj instanceof JSONObject) {
                JSONObject params = (JSONObject) dataObj;
                Iterator<String> keys = params.keys();
                while (keys.hasNext()) {
                    String key = keys.next();
                    JSONObject dataItem = new JSONObject();
                    dataItem.put("identifier", key);
                    dataItem.put("value", params.get(key));
                    result.put(dataItem);
                }
            }
        }
        
        Log.d(TAG, "解析完成，共 " + result.length() + " 条数据");
        return result;
    }
    
    /**
     * 检查轮询器是否正在运行
     */
    public boolean isRunning() {
        return isRunning;
    }
    
    /**
     * 检查轮询器是否正在运行（与isRunning()相同）
     */
    public boolean isPolling() {
        return isRunning;
    }
}