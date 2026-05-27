package com.example.onenet215;

import androidx.room.Entity;
import androidx.room.Index;
import androidx.room.PrimaryKey;

@Entity(tableName = "users", indices = {@Index(value = "username", unique = true)})
public class User {

    @PrimaryKey(autoGenerate = true)
    public long id;

    public String username;

    public String password;

    public String role;
}
