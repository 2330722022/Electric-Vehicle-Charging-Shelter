package com.example.onenet215;

import androidx.room.Dao;
import androidx.room.Insert;
import androidx.room.Query;

import java.util.List;

@Dao
public interface SensorRecordDao {

    @Insert
    long insert(SensorRecord record);

    @Query("SELECT * FROM sensor_records ORDER BY timestamp DESC LIMIT 100")
    List<SensorRecord> getRecentRecords();
}