package com.example.onenet215;

import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.widget.Button;
import android.widget.TextView;
import android.widget.Toast;

import androidx.appcompat.app.AppCompatActivity;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.net.HttpURLConnection;
import java.net.URL;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;

public class MainActivity extends AppCompatActivity {

    private static final String TAG = "MainActivity";
    
    // OneNET 配置信息
    private static final String PRODUCT_ID = "3ffd453bafa04ffbb8775ee13eead513";
    private static final String DEVICE_NAME = "test1";
    private static final String AUTHORIZATION = "67k36rzgOO";
    private static final String API_URL = "https://iot-api.heclouds.com/thingmodel/query-device-property";
    
    private static final int REFRESH_INTERVAL = 2000;
    
    private TextView tvHumidity;
    private TextView tvLight;
    private TextView tvConnectionStatus;
    private TextView tvLastUpdate;
    private Button btnRefresh;
    
    private Handler handler;
    private Runnable refreshRunnable;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);
        
        Log.d(TAG, "=== App Started ===");
        Log.d(TAG, "Product ID: " + PRODUCT_ID);
        Log.d(TAG, "Device Name: " + DEVICE_NAME);
        Log.d(TAG, "API URL: " + API_URL);
        
        initViews();
        initHandler();
        
        // 开机自动获取设备数据
        Log.d(TAG, "Fetching data on startup...");
        fetchDeviceData();
        
        // 启动自动刷新
        startAutoRefresh();
    }
    
    private void initViews() {
        tvHumidity = findViewById(R.id.tvHumidity);
        tvLight = findViewById(R.id.tvLight);
        tvConnectionStatus = findViewById(R.id.tvConnectionStatus);
        tvLastUpdate = findViewById(R.id.tvLastUpdate);
        btnRefresh = findViewById(R.id.btnRefresh);
        
        // 设置按钮点击事件
        btnRefresh.setOnClickListener(v -> {
            Log.d(TAG, "Manual refresh button clicked");
            Toast.makeText(MainActivity.this, "正在刷新数据...", Toast.LENGTH_SHORT).show();
            fetchDeviceData();
        });
    }
    
    private void initHandler() {
        handler = new Handler(Looper.getMainLooper());
        refreshRunnable = new Runnable() {
            @Override
            public void run() {
                Log.d(TAG, "Auto refresh triggered");
                fetchDeviceData();
                handler.postDelayed(this, REFRESH_INTERVAL);
            }
        };
    }
    
    private void startAutoRefresh() {
        handler.postDelayed(refreshRunnable, REFRESH_INTERVAL);
        Log.d(TAG, "Auto refresh started with interval: " + REFRESH_INTERVAL + "ms");
    }
    
    private void stopAutoRefresh() {
        if (handler != null && refreshRunnable != null) {
            handler.removeCallbacks(refreshRunnable);
            Log.d(TAG, "Auto refresh stopped");
        }
    }
    
    private void fetchDeviceData() {
        // 确保在子线程执行网络请求
        Log.d(TAG, "fetchDeviceData called from thread: " + Thread.currentThread().getName());
        
        new Thread(new Runnable() {
            @Override
            public void run() {
                Log.d(TAG, "Network request started in background thread");
                HttpURLConnection connection = null;
                
                try {
                    // 构建请求 URL
                    String urlString = API_URL + "?product_id=" + PRODUCT_ID + "&device_name=" + DEVICE_NAME;
                    Log.d(TAG, "Request URL: " + urlString);
                    
                    URL url = new URL(urlString);
                    connection = (HttpURLConnection) url.openConnection();
                    
                    // 设置请求方法
                    connection.setRequestMethod("GET");
                    Log.d(TAG, "Request Method: GET");
                    
                    // 设置请求头 - 关键修复点
                    connection.setRequestProperty("authorization", AUTHORIZATION);
                    Log.d(TAG, "Authorization Header: " + AUTHORIZATION);
                    
                    // 设置超时时间
                    connection.setConnectTimeout(10000);
                    connection.setReadTimeout(10000);
                    
                    // 启用输入输出
                    connection.setDoInput(true);
                    
                    Log.d(TAG, "Sending request...");
                    
                    // 获取响应码
                    int responseCode = connection.getResponseCode();
                    Log.d(TAG, "Response Code: " + responseCode);
                    
                    if (responseCode == HttpURLConnection.HTTP_OK) {
                        // 读取响应内容
                        BufferedReader reader = new BufferedReader(new InputStreamReader(connection.getInputStream()));
                        StringBuilder result = new StringBuilder();
                        String line;
                        
                        while ((line = reader.readLine()) != null) {
                            result.append(line);
                        }
                        reader.close();
                        
                        String jsonResponse = result.toString();
                        Log.d(TAG, "Response Body: " + jsonResponse);
                        
                        // 解析并显示数据
                        parseAndShowData(jsonResponse);
                        
                        // 更新连接状态
                        runOnUiThread(new Runnable() {
                            @Override
                            public void run() {
                                updateConnectionStatus(true);
                                updateLastUpdateTime();
                            }
                        });
                        
                    } else {
                        // 读取错误响应
                        String errorResponse = "";
                        if (connection.getErrorStream() != null) {
                            BufferedReader errorReader = new BufferedReader(new InputStreamReader(connection.getErrorStream()));
                            StringBuilder errorResult = new StringBuilder();
                            String errorLine;
                            while ((errorLine = errorReader.readLine()) != null) {
                                errorResult.append(errorLine);
                            }
                            errorReader.close();
                            errorResponse = errorResult.toString();
                        }
                        
                        Log.e(TAG, "HTTP Error Code: " + responseCode);
                        Log.e(TAG, "Error Response: " + errorResponse);
                        
                        runOnUiThread(new Runnable() {
                            @Override
                            public void run() {
                                updateConnectionStatus(false);
                                Toast.makeText(MainActivity.this, 
                                    "请求失败: HTTP " + responseCode + "\n" + errorResponse, 
                                    Toast.LENGTH_LONG).show();
                            }
                        });
                    }
                    
                } catch (Exception e) {
                    Log.e(TAG, "Network Exception: " + e.getClass().getName(), e);
                    Log.e(TAG, "Exception Message: " + e.getMessage());
                    
                    runOnUiThread(new Runnable() {
                        @Override
                        public void run() {
                            updateConnectionStatus(false);
                            Toast.makeText(MainActivity.this, 
                                "网络错误: " + e.getClass().getSimpleName() + "\n" + e.getMessage(), 
                                Toast.LENGTH_LONG).show();
                        }
                    });
                    
                } finally {
                    if (connection != null) {
                        connection.disconnect();
                        Log.d(TAG, "Connection disconnected");
                    }
                }
            }
        }).start();
    }
    
    private void parseAndShowData(String jsonResponse) {
        Log.d(TAG, "Parsing JSON response...");
        
        try {
            JSONObject jsonObject = new JSONObject(jsonResponse);
            
            // 检查 errno
            if (jsonObject.has("errno")) {
                int errno = jsonObject.getInt("errno");
                Log.d(TAG, "Errno: " + errno);
                
                if (errno != 0) {
                    String errmsg = jsonObject.optString("errmsg", "未知错误");
                    Log.e(TAG, "OneNET Error: errno=" + errno + ", errmsg=" + errmsg);
                    
                    runOnUiThread(new Runnable() {
                        @Override
                        public void run() {
                            tvHumidity.setText("错误");
                            tvLight.setText("错误");
                            Toast.makeText(MainActivity.this, 
                                "OneNET 错误: " + errmsg + " (errno: " + errno + ")", 
                                Toast.LENGTH_LONG).show();
                        }
                    });
                    return;
                }
            }
            
            // 检查 data 字段
            if (!jsonObject.has("data")) {
                Log.e(TAG, "No 'data' field in response");
                Log.e(TAG, "Full response: " + jsonResponse);
                
                runOnUiThread(new Runnable() {
                    @Override
                    public void run() {
                        tvHumidity.setText("无数据");
                        tvLight.setText("无数据");
                        Toast.makeText(MainActivity.this, "响应中缺少 data 字段", Toast.LENGTH_SHORT).show();
                    }
                });
                return;
            }
            
            Object dataObj = jsonObject.get("data");
            Log.d(TAG, "Data type: " + dataObj.getClass().getName());
            
            if (!(dataObj instanceof JSONArray)) {
                Log.e(TAG, "Data is not a JSONArray");
                Log.e(TAG, "Data value: " + dataObj.toString());
                
                runOnUiThread(new Runnable() {
                    @Override
                    public void run() {
                        tvHumidity.setText("格式错误");
                        tvLight.setText("格式错误");
                        Toast.makeText(MainActivity.this, "数据格式错误", Toast.LENGTH_SHORT).show();
                    }
                });
                return;
            }
            
            JSONArray dataArray = (JSONArray) dataObj;
            Log.d(TAG, "Data array length: " + dataArray.length());
            
            double humidity = 0;
            double light = 0;
            boolean foundHumidity = false;
            boolean foundLight = false;
            
            // 遍历数据数组
            for (int i = 0; i < dataArray.length(); i++) {
                JSONObject item = dataArray.getJSONObject(i);
                Log.d(TAG, "Item " + i + ": " + item.toString());
                
                if (item.has("identifier") && item.has("value")) {
                    String identifier = item.getString("identifier");
                    String value = item.getString("value");
                    
                    Log.d(TAG, "Found property - Identifier: " + identifier + ", Value: " + value);
                    
                    // 匹配湿度数据
                    if ("humidity".equalsIgnoreCase(identifier)) {
                        try {
                            humidity = Double.parseDouble(value);
                            foundHumidity = true;
                            Log.d(TAG, "✓ Humidity found: " + humidity);
                        } catch (NumberFormatException e) {
                            Log.e(TAG, "Invalid humidity value: " + value, e);
                        }
                    }
                    // 匹配光照数据
                    else if ("light".equalsIgnoreCase(identifier)) {
                        try {
                            light = Double.parseDouble(value);
                            foundLight = true;
                            Log.d(TAG, "✓ Light found: " + light);
                        } catch (NumberFormatException e) {
                            Log.e(TAG, "Invalid light value: " + value, e);
                        }
                    }
                } else {
                    Log.w(TAG, "Item missing identifier or value: " + item.toString());
                }
            }
            
            // 更新 UI
            final double finalHumidity = humidity;
            final double finalLight = light;
            final boolean isFoundHumidity = foundHumidity;
            final boolean isFoundLight = foundLight;
            
            runOnUiThread(new Runnable() {
                @Override
                public void run() {
                    if (isFoundHumidity) {
                        tvHumidity.setText(String.format(Locale.getDefault(), "%.1f", finalHumidity));
                        Log.d(TAG, "UI Updated - Humidity: " + finalHumidity);
                    } else {
                        tvHumidity.setText("无数据");
                        Log.w(TAG, "⚠ Humidity data not found in response");
                    }
                    
                    if (isFoundLight) {
                        tvLight.setText(String.format(Locale.getDefault(), "%.1f", finalLight));
                        Log.d(TAG, "UI Updated - Light: " + finalLight);
                    } else {
                        tvLight.setText("无数据");
                        Log.w(TAG, "⚠ Light data not found in response");
                    }
                    
                    if (!isFoundHumidity && !isFoundLight) {
                        Log.w(TAG, "⚠ No target data found (humidity/light)");
                        Toast.makeText(MainActivity.this, 
                            "未找到湿度和光照数据\n请检查设备是否上报了这些数据", 
                            Toast.LENGTH_LONG).show();
                    } else {
                        Log.d(TAG, "✓ Data update successful");
                    }
                }
            });
            
        } catch (JSONException e) {
            Log.e(TAG, "JSON Parse Exception: " + e.getMessage(), e);
            Log.e(TAG, "Failed to parse: " + jsonResponse);
            
            runOnUiThread(new Runnable() {
                @Override
                public void run() {
                    tvHumidity.setText("解析错误");
                    tvLight.setText("解析错误");
                    Toast.makeText(MainActivity.this, 
                        "JSON 解析错误: " + e.getMessage(), 
                        Toast.LENGTH_LONG).show();
                }
            });
        }
    }
    
    private void updateConnectionStatus(boolean connected) {
        if (connected) {
            tvConnectionStatus.setText("已连接");
            tvConnectionStatus.setTextColor(0xFF4CAF50); // Green
            Log.d(TAG, "Connection status: Connected");
        } else {
            tvConnectionStatus.setText("连接失败");
            tvConnectionStatus.setTextColor(0xFFFF5722); // Red
            Log.d(TAG, "Connection status: Failed");
        }
    }
    
    private void updateLastUpdateTime() {
        SimpleDateFormat sdf = new SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.getDefault());
        String currentTime = sdf.format(new Date());
        tvLastUpdate.setText("最后更新：" + currentTime);
        Log.d(TAG, "Last update time: " + currentTime);
    }
    
    @Override
    protected void onDestroy() {
        super.onDestroy();
        stopAutoRefresh();
        Log.d(TAG, "=== App Destroyed ===");
    }
}
