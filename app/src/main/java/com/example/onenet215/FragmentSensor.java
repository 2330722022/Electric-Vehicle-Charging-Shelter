package com.example.onenet215;

import android.app.AlertDialog;
import android.content.Intent;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.view.animation.Animation;
import android.view.animation.ScaleAnimation;
import android.widget.Button;
import android.widget.ImageView;
import android.widget.LinearLayout;

import android.widget.ProgressBar;
import android.widget.TextView;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.cardview.widget.CardView;
import androidx.fragment.app.Fragment;
import androidx.swiperefreshlayout.widget.SwipeRefreshLayout;

import com.github.mikephil.charting.charts.LineChart;
import com.github.mikephil.charting.components.XAxis;
import com.github.mikephil.charting.data.Entry;
import com.github.mikephil.charting.data.LineData;
import com.github.mikephil.charting.data.LineDataSet;
import com.github.mikephil.charting.formatter.ValueFormatter;

import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

public class FragmentSensor extends Fragment {

    private static class CityInfo {
        final String name;
        final double lat;
        final double lon;

        CityInfo(String name, double lat, double lon) {
            this.name = name;
            this.lat = lat;
            this.lon = lon;
        }
    }

    private static final CityInfo[] CITIES = {
            new CityInfo("大连", 38.9140, 121.6147),
            new CityInfo("北京", 39.9042, 116.4074),
            new CityInfo("上海", 31.2304, 121.4737),
            new CityInfo("广州", 23.1291, 113.2644),
            new CityInfo("深圳", 22.5431, 114.0579),
            new CityInfo("成都", 30.5728, 104.0668),
            new CityInfo("杭州", 30.2741, 120.1551),
            new CityInfo("武汉", 30.5928, 114.3055),
            new CityInfo("南京", 32.0603, 118.7969),
            new CityInfo("重庆", 29.4316, 106.9123),
            new CityInfo("天津", 39.3434, 117.3616),
            new CityInfo("西安", 34.3416, 108.9398),
            new CityInfo("长沙", 28.2282, 112.9388),
            new CityInfo("郑州", 34.7466, 113.6253),
            new CityInfo("济南", 36.6512, 117.1201),
            new CityInfo("青岛", 36.0671, 120.3826),
            new CityInfo("沈阳", 41.8057, 123.4315),
            new CityInfo("哈尔滨", 45.8038, 126.5350),
            new CityInfo("合肥", 31.8206, 117.2272),
            new CityInfo("福州", 26.0745, 119.2965),
            new CityInfo("厦门", 24.4798, 118.0894),
            new CityInfo("昆明", 25.0389, 102.7183),
            new CityInfo("贵阳", 26.6470, 106.6302),
            new CityInfo("兰州", 36.0611, 103.8343),
            new CityInfo("南宁", 22.8170, 108.3665),
            new CityInfo("海口", 20.0200, 110.3308),
            new CityInfo("石家庄", 38.0428, 114.5149),
            new CityInfo("太原", 37.8706, 112.5509),
            new CityInfo("呼和浩特", 40.8424, 111.7490),
            new CityInfo("长春", 43.8178, 125.3235),
            new CityInfo("南昌", 28.6820, 115.8582),
            new CityInfo("乌鲁木齐", 43.8256, 87.6168),
            new CityInfo("拉萨", 29.6500, 91.1000),
            new CityInfo("银川", 38.4872, 106.2309),
            new CityInfo("西宁", 36.6171, 101.7785),
    };

    private static final double DEFAULT_LAT = 38.9140;
    private static final double DEFAULT_LON = 121.6147;
    private static final String DEFAULT_CITY = "大连";

    private TextView tvTemperature, tvHumidity, tvLight, tvPm25, tvSmoke, tvFlame;
    private TextView tvAlarmState, tvAlarmDetail, tvLastUpdate;
    private TextView tvDeviceOnlineStatus, tvDataRefreshRate;
    private TextView tvWeatherInfo, tvWeatherCity, tvWeatherTemp, tvWeatherHumidity;
    private TextView tvWeatherHint, tvHighTempWarning;
    private ProgressBar pbWeatherLoading;
    private LinearLayout llHighTempWarning;
    private Button btnSensorLowerThreshold;
    private CardView cardAlarm, cardWeather;
    private ImageView ivAlarmIndicator;
    private TextView tvHistoryEntry;
    private TextView tvWorkModeLabel;

    private SwipeRefreshLayout swipeRefreshSensor;
    private LineChart sensorTrendChart;
    private final List<Entry> tempHistory = new ArrayList<>();
    private int trendIndex = 0;

    private String humidity_value, light_value, temperature_value;
    private String pm2_5_value, mq2_value, flame_value;
    private int alarm_state;
    private boolean workModeAuto = true;

    private String lastTempValue = "";
    private String lastHumidityValue = "";
    private String lastLightValue = "";
    private String lastPm25Value = "";
    private String lastSmokeValue = "";
    private String lastFlameValue = "";

    private WeatherApiClient weatherClient;
    private final Handler mainHandler = new Handler(Looper.getMainLooper());

    private String currentCityName = DEFAULT_CITY;
    private double currentCityLat = DEFAULT_LAT;
    private double currentCityLon = DEFAULT_LON;

    private ControlCallback controlCallback;

    public interface ControlCallback {
        void onControlDevice(String property, Object value);
    }

    public void setControlCallback(ControlCallback callback) {
        this.controlCallback = callback;
    }

    private MainActivity getMain() {
        return (MainActivity) getActivity();
    }

    @Nullable
    @Override
    public View onCreateView(@NonNull LayoutInflater inflater, @Nullable ViewGroup container,
                             @Nullable Bundle savedInstanceState) {
        View view = inflater.inflate(R.layout.fragment_sensor, container, false);

        tvTemperature = view.findViewById(R.id.tvTemperature);
        tvHumidity = view.findViewById(R.id.tvHumidity);
        tvLight = view.findViewById(R.id.tvLight);
        tvPm25 = view.findViewById(R.id.tvPm25);
        tvSmoke = view.findViewById(R.id.tvSmoke);
        tvFlame = view.findViewById(R.id.tvFlame);
        tvAlarmState = view.findViewById(R.id.tvAlarmState);
        tvAlarmDetail = view.findViewById(R.id.tvAlarmDetail);
        tvLastUpdate = view.findViewById(R.id.tvLastUpdate);
        tvDeviceOnlineStatus = view.findViewById(R.id.tvDeviceOnlineStatus);
        tvDataRefreshRate = view.findViewById(R.id.tvDataRefreshRate);
        tvWeatherInfo = view.findViewById(R.id.tvWeatherInfo);
        tvWeatherCity = view.findViewById(R.id.tvWeatherCity);
        tvWeatherTemp = view.findViewById(R.id.tvWeatherTemp);
        tvWeatherHumidity = view.findViewById(R.id.tvWeatherHumidity);
        tvWeatherHint = view.findViewById(R.id.tvWeatherHint);
        tvHighTempWarning = view.findViewById(R.id.tvHighTempWarning);
        pbWeatherLoading = view.findViewById(R.id.pbWeatherLoading);
        llHighTempWarning = view.findViewById(R.id.llHighTempWarning);
        btnSensorLowerThreshold = view.findViewById(R.id.btnSensorLowerThreshold);
        cardAlarm = view.findViewById(R.id.cardAlarm);
        cardWeather = view.findViewById(R.id.cardWeather);
        ivAlarmIndicator = view.findViewById(R.id.ivAlarmIndicator);
        tvHistoryEntry = view.findViewById(R.id.tvHistoryEntry);

        tvWorkModeLabel = view.findViewById(R.id.tvWorkModeLabel);

        swipeRefreshSensor = view.findViewById(R.id.swipeRefreshSensor);
        swipeRefreshSensor.setColorSchemeResources(
                android.R.color.holo_blue_dark,
                android.R.color.holo_red_dark,
                android.R.color.holo_orange_dark);
        swipeRefreshSensor.setOnRefreshListener(() -> {
            getMain().requestRefresh();
            swipeRefreshSensor.setRefreshing(false);
        });

        sensorTrendChart = view.findViewById(R.id.sensorTrendChart);
        initTrendChart();

        tvHistoryEntry.setOnClickListener(v -> {
            startActivity(new Intent(getActivity(), HistoryActivity.class));
        });

        btnSensorLowerThreshold.setOnClickListener(v -> {
            if (controlCallback != null) {
                controlCallback.onControlDevice("temp_threshold", 45);
                llHighTempWarning.setVisibility(View.GONE);
                ToastUtils.show(getActivity(), "温度阈值已调低至 45°C");
            }
        });

        cardWeather.setOnClickListener(v -> showCityPickerDialog());

        weatherClient = new WeatherApiClient();
        fetchWeatherForCity(currentCityName, currentCityLat, currentCityLon);

        return view;
    }

    private void showCityPickerDialog() {
        String[] cityNames = new String[CITIES.length];
        for (int i = 0; i < CITIES.length; i++) {
            cityNames[i] = CITIES[i].name;
        }

        new AlertDialog.Builder(getActivity())
                .setTitle("📍 选择城市（天气数据实时从API获取）")
                .setItems(cityNames, (dialog, which) -> {
                    CityInfo city = CITIES[which];
                    if (!city.name.equals(currentCityName)) {
                        fetchWeatherForCity(city.name, city.lat, city.lon);
                    }
                })
                .setNegativeButton("取消", null)
                .show();
    }

    private void fetchWeatherForCity(String cityName, double lat, double lon) {
        this.currentCityName = cityName;
        this.currentCityLat = lat;
        this.currentCityLon = lon;

        if (tvWeatherCity != null) {
            tvWeatherCity.setText(cityName);
        }
        if (tvWeatherInfo != null) {
            tvWeatherInfo.setText("正在请求 Open-Meteo API...");
        }
        if (tvWeatherTemp != null) {
            tvWeatherTemp.setText("--°");
        }
        if (tvWeatherHumidity != null) {
            tvWeatherHumidity.setText("--%");
        }
        if (tvWeatherHint != null) {
            tvWeatherHint.setText("⏳ 正在从服务器获取实时天气...");
        }
        if (pbWeatherLoading != null) {
            pbWeatherLoading.setVisibility(View.VISIBLE);
        }

        weatherClient.fetchWeather(lat, lon, new WeatherApiClient.WeatherCallback() {
            @Override
            public void onSuccess(WeatherInfo info) {
                mainHandler.post(() -> {
                    if (pbWeatherLoading != null) {
                        pbWeatherLoading.setVisibility(View.GONE);
                    }
                    handleWeatherUpdate(info);
                });
            }

            @Override
            public void onError(String error) {
                Log.w("FragmentSensor", "天气获取失败: " + error);
                mainHandler.post(() -> {
                    if (pbWeatherLoading != null) {
                        pbWeatherLoading.setVisibility(View.GONE);
                    }
                    if (tvWeatherInfo != null) {
                        tvWeatherInfo.setText("⚠ " + error);
                    }
                    if (tvWeatherHint != null) {
                        tvWeatherHint.setText("❌ API请求失败 · 点击重试");
                    }
                });
            }
        });
    }

    private void handleWeatherUpdate(WeatherInfo info) {
        if (tvWeatherInfo == null) return;

        String weatherDesc = info.getWeatherIcon() + " "
                + info.getWindDirectionText()
                + String.format(Locale.getDefault(), " %.0f km/h", info.windSpeed)
                + " | 今日 " + String.format(Locale.getDefault(), "%.0f", info.todayMinTemp)
                + "~" + String.format(Locale.getDefault(), "%.0f°C", info.todayMaxTemp);

        tvWeatherInfo.setText(weatherDesc);
        tvWeatherTemp.setText(String.format(Locale.getDefault(), "%.0f°", info.temperature));
        tvWeatherHumidity.setText(String.format(Locale.getDefault(), "%.0f%%", info.humidity));

        if (tvWeatherHint != null) {
            tvWeatherHint.setText("✅ Open-Meteo实时数据 · 更新时间 " + info.weatherTimestamp);
        }

        if (info.todayMaxTemp > 35.0 && llHighTempWarning != null) {
            String warningText = "🌡️ 高温环境预警：今日预报最高气温 "
                    + String.format(Locale.getDefault(), "%.0f°C", info.todayMaxTemp)
                    + "，超过35°C安全线。建议将网关硬件温度告警阈值调低至 45°C，以防电池热失控误报。";
            tvHighTempWarning.setText(warningText);
            llHighTempWarning.setVisibility(View.VISIBLE);
        }
    }

    public void updateSensorData(String temperature, String humidity, String light,
                                  String pm25, String mq2, String flame) {
        this.temperature_value = temperature;
        this.humidity_value = humidity;
        this.light_value = light;
        this.pm2_5_value = pm25;
        this.mq2_value = mq2;
        this.flame_value = flame;
    }

    public void setWorkMode(boolean auto) {
        this.workModeAuto = auto;
        if (tvWorkModeLabel != null) {
            tvWorkModeLabel.setText(auto ? "🤖 自动运行中" : "🔧 手动模式");
            tvWorkModeLabel.setTextColor(auto ? 0xFF4CAF50 : 0xFFFF9800);
        }
    }

    private void initTrendChart() {
        sensorTrendChart.getDescription().setEnabled(false);
        sensorTrendChart.setTouchEnabled(false);
        sensorTrendChart.setDragEnabled(false);
        sensorTrendChart.setScaleEnabled(false);
        sensorTrendChart.setPinchZoom(false);
        sensorTrendChart.getXAxis().setPosition(XAxis.XAxisPosition.BOTTOM);
        sensorTrendChart.getXAxis().setDrawGridLines(false);
        sensorTrendChart.getXAxis().setDrawLabels(false);
        sensorTrendChart.getAxisLeft().setDrawGridLines(true);
        sensorTrendChart.getAxisLeft().setGridColor(0xFFEEEEEE);
        sensorTrendChart.getAxisLeft().setTextColor(0xFF9E9E9E);
        sensorTrendChart.getAxisLeft().setTextSize(9f);
        sensorTrendChart.getAxisRight().setEnabled(false);
        sensorTrendChart.getLegend().setEnabled(false);
        sensorTrendChart.setData(new LineData());
    }

    private void updateTrendChart(String tempStr) {
        try {
            float temp = Float.parseFloat(tempStr);
            tempHistory.add(new Entry(trendIndex++, temp));
            if (tempHistory.size() > 20) tempHistory.remove(0);

            LineDataSet set = new LineDataSet(tempHistory, "温度");
            set.setColor(0xFFE53935);
            set.setCircleColor(0xFFE53935);
            set.setCircleRadius(2f);
            set.setLineWidth(2f);
            set.setMode(LineDataSet.Mode.CUBIC_BEZIER);
            set.setDrawValues(false);
            set.setDrawFilled(true);
            set.setFillDrawable(new android.graphics.drawable.GradientDrawable(
                    android.graphics.drawable.GradientDrawable.Orientation.TOP_BOTTOM,
                    new int[]{0x33E53935, 0x00E53935}));

            sensorTrendChart.setData(new LineData(set));
            sensorTrendChart.notifyDataSetChanged();
            sensorTrendChart.setVisibleXRangeMaximum(10);
            sensorTrendChart.moveViewToX(trendIndex - 1);
            sensorTrendChart.invalidate();
        } catch (NumberFormatException ignored) {}
    }

    public void updateAlarmState(int state, int tempThreshold, int pm25Threshold) {
        this.alarm_state = state;

        if (tvAlarmState == null) return;

        int cardColor, textColor;
        String alarmText, alarmDetail;

        switch (state) {
            case 0:
                ivAlarmIndicator.setImageResource(R.drawable.ic_circle_green);
                alarmText = "系统正常";
                alarmDetail = "充电棚运行中 · 无异常";
                cardColor = 0xFFC8E6C9;
                textColor = 0xFF2E7D32;
                break;
            case 1:
                ivAlarmIndicator.setImageResource(R.drawable.ic_circle_red);
                alarmText = "⚠ 温度报警";
                alarmDetail = "温度超过阈值 (" + tempThreshold + "°C)";
                cardColor = 0xFFFFCDD2;
                textColor = 0xFFC62828;
                break;
            case 2:
                ivAlarmIndicator.setImageResource(R.drawable.ic_circle_red);
                alarmText = "⚠ 烟雾/火焰报警";
                alarmDetail = "检测到烟雾或火焰";
                cardColor = 0xFFFFCDD2;
                textColor = 0xFFC62828;
                break;
            case 3:
                ivAlarmIndicator.setImageResource(R.drawable.ic_circle_red);
                alarmText = "⚠ PM2.5超标";
                alarmDetail = "PM2.5超过阈值 (" + pm25Threshold + "μg/m³)";
                cardColor = 0xFFFFE0B2;
                textColor = 0xFFE65100;
                break;
            case 4:
                ivAlarmIndicator.setImageResource(R.drawable.ic_circle_red);
                alarmText = "⚠ 振动告警";
                alarmDetail = "检测到异常振动";
                cardColor = 0xFFFFE0B2;
                textColor = 0xFFE65100;
                break;
            default:
                ivAlarmIndicator.setImageResource(R.drawable.ic_circle_green);
                alarmText = "系统正常";
                alarmDetail = "充电棚运行中 · 无异常";
                cardColor = 0xFFC8E6C9;
                textColor = 0xFF2E7D32;
                break;
        }

        tvAlarmState.setText(alarmText);
        tvAlarmDetail.setText(alarmDetail);
        tvAlarmState.setTextColor(textColor);
        tvAlarmDetail.setTextColor(textColor);
        cardAlarm.setCardBackgroundColor(cardColor);
    }

    public void updateLastTime(String time) {
        if (tvLastUpdate != null) {
            tvLastUpdate.setText(time);
        }
    }

    private void animateValueChange(TextView tv, String newVal, String oldVal) {
        if (tv == null) return;
        if (newVal != null && !newVal.equals(oldVal) && !tv.getText().toString().equals(newVal)) {
            ScaleAnimation anim = new ScaleAnimation(1.2f, 1f, 1.2f, 1f,
                    Animation.RELATIVE_TO_SELF, 0.5f,
                    Animation.RELATIVE_TO_SELF, 0.5f);
            anim.setDuration(300);
            tv.startAnimation(anim);
        }
    }

    public void refreshUI(boolean isOnline) {
        if (tvTemperature == null || getActivity() == null) return;

        animateValueChange(tvTemperature, temperature_value, lastTempValue);
        lastTempValue = temperature_value;
        tvTemperature.setText(temperature_value != null ? temperature_value : "--");
        if (temperature_value != null && sensorTrendChart != null) {
            updateTrendChart(temperature_value);
        }

        animateValueChange(tvHumidity, humidity_value, lastHumidityValue);
        lastHumidityValue = humidity_value;
        tvHumidity.setText(humidity_value != null ? humidity_value : "--");

        animateValueChange(tvLight, light_value, lastLightValue);
        lastLightValue = light_value;
        tvLight.setText(light_value != null ? light_value : "--");

        animateValueChange(tvPm25, pm2_5_value, lastPm25Value);
        lastPm25Value = pm2_5_value;
        tvPm25.setText(pm2_5_value != null ? pm2_5_value : "--");

        animateValueChange(tvSmoke, mq2_value, lastSmokeValue);
        lastSmokeValue = mq2_value;
        tvSmoke.setText(mq2_value != null ? mq2_value : "--");

        animateValueChange(tvFlame, flame_value, lastFlameValue);
        lastFlameValue = flame_value;
        tvFlame.setText(flame_value != null ? flame_value : "--");

        if (isOnline) {
            tvTemperature.setTextColor(0xFFE53935);
            tvHumidity.setTextColor(0xFF1E88E5);
            tvLight.setTextColor(0xFFFF8F00);
            tvPm25.setTextColor(0xFF7B1FA2);
            tvSmoke.setTextColor(0xFF795548);
            tvFlame.setTextColor(0xFFD50000);

            if (tvDeviceOnlineStatus != null) {
                tvDeviceOnlineStatus.setText("✅ 在线");
                tvDeviceOnlineStatus.setTextColor(0xFF4CAF50);
            }
            if (tvDataRefreshRate != null) {
                tvDataRefreshRate.setText("实时");
                tvDataRefreshRate.setTextColor(0xFF4CAF50);
            }
        } else {
            int gray = 0xFFBDBDBD;
            tvTemperature.setTextColor(gray);
            tvHumidity.setTextColor(gray);
            tvLight.setTextColor(gray);
            tvPm25.setTextColor(gray);
            tvSmoke.setTextColor(gray);
            tvFlame.setTextColor(gray);

            if (tvDeviceOnlineStatus != null) {
                tvDeviceOnlineStatus.setText("⚠ 离线");
                tvDeviceOnlineStatus.setTextColor(0xFFFF5722);
            }
            if (tvDataRefreshRate != null) {
                tvDataRefreshRate.setText("--");
                tvDataRefreshRate.setTextColor(gray);
            }
        }
    }
}
