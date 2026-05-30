package com.example.onenet215;

import android.graphics.Color;
import android.graphics.drawable.Drawable;
import android.os.Bundle;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.TextView;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AppCompatActivity;
import androidx.recyclerview.widget.LinearLayoutManager;
import androidx.recyclerview.widget.RecyclerView;

import com.github.mikephil.charting.charts.LineChart;
import com.github.mikephil.charting.components.Legend;
import com.github.mikephil.charting.components.XAxis;
import com.github.mikephil.charting.components.YAxis;
import com.github.mikephil.charting.data.Entry;
import com.github.mikephil.charting.data.LineData;
import com.github.mikephil.charting.data.LineDataSet;
import com.github.mikephil.charting.formatter.ValueFormatter;
import com.github.mikephil.charting.interfaces.datasets.ILineDataSet;

import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;
import java.util.List;
import java.util.Locale;
import java.util.concurrent.Executors;

public class HistoryActivity extends AppCompatActivity {

    private LineChart lineChart;
    private RecyclerView rvHistoryRecords;
    private TextView tvRecordCount;
    private RecordAdapter adapter;
    private final java.util.concurrent.ExecutorService ioExecutor = Executors.newSingleThreadExecutor();
    private final SimpleDateFormat sdf = new SimpleDateFormat("MM-dd HH:mm", Locale.getDefault());

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_history);

        lineChart = findViewById(R.id.lineChart);
        rvHistoryRecords = findViewById(R.id.rvHistoryRecords);
        tvRecordCount = findViewById(R.id.tvRecordCount);

        findViewById(R.id.tvBack).setOnClickListener(v -> finish());

        rvHistoryRecords.setLayoutManager(new LinearLayoutManager(this));
        adapter = new RecordAdapter();
        rvHistoryRecords.setAdapter(adapter);
        rvHistoryRecords.setNestedScrollingEnabled(false);

        loadHistoryData();
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        if (ioExecutor != null) ioExecutor.shutdownNow();
    }

    private void loadHistoryData() {
        ioExecutor.execute(() -> {
            AppDatabase db = AppDatabase.getInstance(this);
            List<SensorRecord> records = db.sensorRecordDao().getRecentRecords();

            runOnUiThread(() -> {
                tvRecordCount.setText(records.size() + " 条记录");
                setupChart(records);
                adapter.setRecords(records);
                fixRecyclerViewHeight(records.size());
            });
        });
    }

    private void fixRecyclerViewHeight(int itemCount) {
        if (itemCount == 0 || adapter.getItemCount() == 0 || rvHistoryRecords.getWidth() == 0) return;
        View itemView = LayoutInflater.from(this).inflate(R.layout.item_history_record, rvHistoryRecords, false);
        itemView.measure(
                View.MeasureSpec.makeMeasureSpec(rvHistoryRecords.getWidth(), View.MeasureSpec.EXACTLY),
                View.MeasureSpec.makeMeasureSpec(0, View.MeasureSpec.UNSPECIFIED));
        int itemHeight = itemView.getMeasuredHeight();
        if (itemHeight <= 0) return;
        ViewGroup.LayoutParams lp = rvHistoryRecords.getLayoutParams();
        lp.height = itemHeight * itemCount;
        rvHistoryRecords.setLayoutParams(lp);
    }

    private void setupChart(List<SensorRecord> records) {
        if (records.isEmpty()) {
            lineChart.clear();
            lineChart.setNoDataText("暂无历史数据");
            return;
        }

        List<Entry> tempEntries = new ArrayList<>();
        List<Entry> smokeEntries = new ArrayList<>();
        List<String> xLabels = new ArrayList<>();

        for (int i = 0; i < records.size(); i++) {
            SensorRecord r = records.get(records.size() - 1 - i);
            float x = i;
            tempEntries.add(new Entry(x, (float) r.temperature));
            smokeEntries.add(new Entry(x, (float) r.mq2));
            xLabels.add(sdf.format(new Date(r.timestamp)));
        }

        LineDataSet tempSet = new LineDataSet(tempEntries, "温度 °C");
        tempSet.setColor(Color.parseColor("#E53935"));
        tempSet.setCircleColor(Color.parseColor("#E53935"));
        tempSet.setCircleRadius(3f);
        tempSet.setLineWidth(2.5f);
        tempSet.setMode(LineDataSet.Mode.CUBIC_BEZIER);
        tempSet.setDrawValues(false);
        tempSet.setDrawFilled(true);
        tempSet.setFillDrawable(createGradient(Color.parseColor("#FFCDD2"), Color.TRANSPARENT));

        LineDataSet smokeSet = new LineDataSet(smokeEntries, "烟雾 MQ-2");
        smokeSet.setColor(Color.parseColor("#795548"));
        smokeSet.setCircleColor(Color.parseColor("#795548"));
        smokeSet.setCircleRadius(3f);
        smokeSet.setLineWidth(2.5f);
        smokeSet.setMode(LineDataSet.Mode.CUBIC_BEZIER);
        smokeSet.setDrawValues(false);
        smokeSet.setDrawFilled(true);
        smokeSet.setFillDrawable(createGradient(Color.parseColor("#D7CCC8"), Color.TRANSPARENT));

        LineData lineData = new LineData(tempSet, smokeSet);
        lineChart.setData(lineData);

        lineChart.getDescription().setEnabled(false);
        lineChart.setTouchEnabled(true);
        lineChart.setDragEnabled(true);
        lineChart.setScaleEnabled(true);
        lineChart.setPinchZoom(true);
        lineChart.setDoubleTapToZoomEnabled(true);
        lineChart.getXAxis().setPosition(XAxis.XAxisPosition.BOTTOM);
        lineChart.getXAxis().setGranularity(1f);
        lineChart.getXAxis().setDrawGridLines(false);
        lineChart.getXAxis().setLabelCount(5, true);
        lineChart.getAxisLeft().setDrawGridLines(true);
        lineChart.getAxisLeft().setGridColor(Color.parseColor("#E0E0E0"));
        lineChart.getAxisRight().setEnabled(false);

        Legend legend = lineChart.getLegend();
        legend.setTextColor(Color.parseColor("#37474F"));
        legend.setTextSize(11f);

        lineChart.animateX(800);
        lineChart.invalidate();
    }

    private Drawable createGradient(int startColor, int endColor) {
        android.graphics.drawable.GradientDrawable gd = new android.graphics.drawable.GradientDrawable(
                android.graphics.drawable.GradientDrawable.Orientation.TOP_BOTTOM,
                new int[]{startColor, endColor}
        );
        gd.setCornerRadius(0);
        return gd;
    }

    private class RecordAdapter extends RecyclerView.Adapter<RecordAdapter.ViewHolder> {

        private List<SensorRecord> records = new ArrayList<>();

        void setRecords(List<SensorRecord> records) {
            this.records = records;
            notifyDataSetChanged();
        }

        @NonNull
        @Override
        public ViewHolder onCreateViewHolder(@NonNull ViewGroup parent, int viewType) {
            View view = LayoutInflater.from(parent.getContext())
                    .inflate(R.layout.item_history_record, parent, false);
            return new ViewHolder(view);
        }

        @Override
        public void onBindViewHolder(@NonNull ViewHolder holder, int position) {
            SensorRecord r = records.get(position);
            holder.tvTime.setText(sdf.format(new Date(r.timestamp)));
            holder.tvTemp.setText(String.format(Locale.getDefault(), "%.1f°C", r.temperature));
            holder.tvHumidity.setText(String.format(Locale.getDefault(), "%.0f%%", r.humidity));
            holder.tvSmoke.setText(String.format(Locale.getDefault(), "%.0f", r.mq2));
            holder.tvPm25.setText(String.format(Locale.getDefault(), "%.0f", r.pm2_5));

            if (r.alarmType == 0) {
                holder.tvAlarm.setText("正常");
                holder.tvAlarm.setTextColor(Color.parseColor("#4CAF50"));
                holder.tvAlarm.setBackgroundColor(Color.parseColor("#E8F5E9"));
            } else {
                holder.tvAlarm.setText("告警 " + r.alarmType);
                holder.tvAlarm.setTextColor(Color.parseColor("#C62828"));
                holder.tvAlarm.setBackgroundColor(Color.parseColor("#FFEBEE"));
            }
        }

        @Override
        public int getItemCount() {
            return records.size();
        }

        class ViewHolder extends RecyclerView.ViewHolder {
            TextView tvTime, tvAlarm, tvTemp, tvHumidity, tvSmoke, tvPm25;

            ViewHolder(View itemView) {
                super(itemView);
                tvTime = itemView.findViewById(R.id.tvRecordTime);
                tvAlarm = itemView.findViewById(R.id.tvRecordAlarm);
                tvTemp = itemView.findViewById(R.id.tvRecordTemp);
                tvHumidity = itemView.findViewById(R.id.tvRecordHumidity);
                tvSmoke = itemView.findViewById(R.id.tvRecordSmoke);
                tvPm25 = itemView.findViewById(R.id.tvRecordPm25);
            }
        }
    }
}