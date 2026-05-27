package com.example.onenet215;

import androidx.room.Entity;
import androidx.room.PrimaryKey;

@Entity(tableName = "sensor_records")
public class SensorRecord {

    @PrimaryKey(autoGenerate = true)
    public long id;

    public long timestamp;

    public double temperature;

    public double humidity;

    public double mq2;

    public double pm2_5;

    public double flame;

    public int alarmType;
}
