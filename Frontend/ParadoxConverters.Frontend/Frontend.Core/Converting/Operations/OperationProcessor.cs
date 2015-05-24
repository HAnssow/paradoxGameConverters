﻿using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Threading;
using Caliburn.Micro;
using Frontend.Core.Logging;

namespace Frontend.Core.Converting.Operations
{
    public class OperationProcessor : DispatcherObject, IOperationProcessor
    {
        private readonly IEventAggregator eventAggregator;

        private readonly Dictionary<OperationResultState, Action<OperationResult, IOperationViewModel>>
            resultHandlershandle;

        public OperationProcessor(IEventAggregator eventAggregator)
        {
            this.eventAggregator = eventAggregator;
            resultHandlershandle = new Dictionary<OperationResultState, Action<OperationResult, IOperationViewModel>>
            {
                {OperationResultState.Success, HandleSuccess},
                {OperationResultState.Warning, HandleWarning},
                {OperationResultState.Error, HandleErrors},
                {OperationResultState.Canceled, HandleCancellation}
            };
        }

        public Task<AggregateOperationsResult> ProcessQueue(IEnumerable<IOperationViewModel> operations,
            CancellationToken token)
        {
            var currentCount = 0;

            foreach (var operation in operations)
            {
                if (token.IsCancellationRequested)
                {
                    break;
                }

                currentCount++;

                operation.State = OperationState.InProgress;
                eventAggregator.PublishOnUIThread(new LogEntry(operation.Description, LogEntrySeverity.Info,
                    LogEntrySource.UI));

                var result = new OperationResult();

                try
                {
                    token.ThrowIfCancellationRequested();
                    result = operation.Process();

                    LogResultMessages(result);
                    resultHandlershandle[result.State](result, operation);
                }
                catch (OperationCanceledException)
                {
                    resultHandlershandle[OperationResultState.Canceled](result, operation);
                }
                catch (Exception e)
                {
                    eventAggregator.PublishOnUIThread(
                        new LogEntry(string.Format("Unhandled exception occurred: {0}", e.Message),
                            LogEntrySeverity.Error, LogEntrySource.UI));
                }
            }

            return Task.FromResult(CalculateResult(operations));
        }

        private AggregateOperationsResult CalculateResult(IEnumerable<IOperationViewModel> operations)
        {
            var operationStates = from operation in operations select operation.State;

            if (operationStates.Contains(OperationState.CompleteWithErrors))
            {
                return AggregateOperationsResult.CompletedWithErrors;
            }
            if (operationStates.Contains(OperationState.Cancelled))
            {
                return AggregateOperationsResult.Canceled;
            }

            return AggregateOperationsResult.CompletedSuccessfully;
        }

        private void HandleCancellation(OperationResult result, IOperationViewModel operation)
        {
            eventAggregator.PublishOnUIThread(
                new LogEntry(string.Format("Operation {0} canceled.", operation.Description), LogEntrySeverity.Warning,
                    LogEntrySource.UI));
        }

        private void HandleSuccess(OperationResult result, IOperationViewModel operation)
        {
            operation.State = OperationState.Success;
            eventAggregator.PublishOnUIThread(
                new LogEntry(string.Format("{0} completed successfully.", operation.Description), LogEntrySeverity.Info,
                    LogEntrySource.UI));
        }

        private void HandleWarning(OperationResult result, IOperationViewModel operation)
        {
            operation.State = OperationState.CompleteWithWarnings;
            eventAggregator.PublishOnUIThread(
                new LogEntry(string.Format("{0} completed with warnings!", operation.Description),
                    LogEntrySeverity.Warning, LogEntrySource.UI));
        }

        private void HandleErrors(OperationResult result, IOperationViewModel operation)
        {
            operation.State = OperationState.CompleteWithErrors;
            eventAggregator.PublishOnUIThread(new LogEntry(string.Format("{0} failed!", operation.Description),
                LogEntrySeverity.Error, LogEntrySource.UI));
        }

        private void LogResultMessages(OperationResult result)
        {
            foreach (var logEntry in result.LogEntries)
            {
                eventAggregator.PublishOnUIThread(logEntry);
            }
        }
    }
}