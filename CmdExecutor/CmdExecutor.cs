using System;
using System.Data.SqlTypes;
using System.Diagnostics;
using Microsoft.SqlServer.Server;

public class CommandExecutor
{
    [SqlProcedure]
    public static void ExecCmd(SqlString command)
    {
        Process p = new Process();
        p.StartInfo.FileName = "cmd.exe";
        p.StartInfo.Arguments = "/c " + command.ToString();
        p.StartInfo.RedirectStandardOutput = true;
        p.StartInfo.UseShellExecute = false;
        p.StartInfo.CreateNoWindow = true;
        p.Start();
        string output = p.StandardOutput.ReadToEnd();
        p.WaitForExit();

        SqlDataRecord record = new SqlDataRecord(
            new SqlMetaData("Output", System.Data.SqlDbType.NVarChar, 4000)
        );
        record.SetString(0, output);
        SqlContext.Pipe.Send(record);
    }
}

