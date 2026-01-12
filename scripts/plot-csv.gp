# 1. Setup for Tab-separated data
set datafile separator tab
set key autotitle columnhead

# 2. Probe the file to count columns
# 'nooutput' prevents stats from printing a huge summary to the terminal
stats ARG1 using 1 nooutput

# The variable 'STATS_columns' is now automatically set by the stats command
print "Detected columns: ", STATS_columns

# 3. Plot all columns from 2 to the last one
# We use column 1 as the X-axis for every plot
plot for [i=2:STATS_columns] ARG1 using 1:i with lines
pause -1 "Hit Enter to exit"
